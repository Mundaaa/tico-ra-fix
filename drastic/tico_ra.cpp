// RetroAchievements for Drastic (see tico_ra.h).
//
// Drastic is a closed Android core, so it has no memory API. Its DS main RAM
// lives in memory the host hands it (the ashmem arenas in libc_shim.c, or the
// module's own BSS): the RAM is found there by the cartridge header the DS boot
// copies to 0x027FFE00, i.e. offset 0x3FFE00 of the 4 MiB main RAM, next to the
// cartridge chip id it also stores at 0x027FF800 and 0x027FFC00.

#include "tico_ra.h"

#include <switch.h>
#include <curl/curl.h>
#include <rc_client.h>
#include <rc_consoles.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "TicoSession.h"
#include "overlay/overlay_ui.h"
#include "../so_util.h"

extern "C" {
extern so_module emu_mod;
int ashmem_region_at(int index, void **raw, size_t *size);
bool switchStorageSocketReady(void);
}

namespace {

constexpr uint32_t kRamSize = 0x400000;
constexpr uint32_t kHeaderOffset = 0x3FFE00;
constexpr uint32_t kChipId1 = 0x3FF800, kChipId2 = 0x3FFC00;

rc_client_t *s_client;
const uint8_t *s_ram;
uint8_t s_header[0x10];
std::string s_rom_path;
bool s_logged_in, s_game_loaded;
unsigned s_frames;

// libnx primitives, not std::thread: the host swaps in Android-style TLS
// (pthr_install_fake_tls) before the game starts.
Mutex s_job_mutex;
CondVar s_job_cond;
std::deque<std::function<void()>> s_jobs;      // worker thread: HTTP
Mutex s_done_mutex;
Thread s_worker;
bool s_worker_started, s_stopping, s_own_sockets;
std::deque<std::function<void()>> s_done;      // host thread: rcheevos callbacks

void Log(const char *fmt, ...) {
  FILE *f = std::fopen("sdmc:/tico/debug/drastic_ra.txt", "a");
  if (!f)
    return;
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fputc('\n', f);
  std::fclose(f);
}

void Toast(const std::string &text) {
  SwitchFrontend::OverlayUI::ShowToast(text, SwitchFrontend::OverlayUI::ToastCorner::TopRight);
}

// ---- finding the DS main RAM ------------------------------------------------

bool LooksLikeRam(const uint8_t *base) {
  uint32_t a, b;
  std::memcpy(&a, base + kChipId1, 4);
  std::memcpy(&b, base + kChipId2, 4);
  return a != 0 && a == b;
}

// The candidate whose header copy and chip ids fit, scanning [begin, end).
const uint8_t *ScanRange(const uint8_t *begin, size_t size, int *matches) {
  if (size < kRamSize)
    return nullptr;
  const uint8_t *found = nullptr;
  const uint8_t *p = begin + kHeaderOffset;
  const uint8_t *const last = begin + size - sizeof(s_header);
  while (p <= last && (p = static_cast<const uint8_t *>(std::memchr(p, s_header[0], last - p + 1)))) {
    if (!(reinterpret_cast<uintptr_t>(p) & 3) && !std::memcmp(p, s_header, sizeof(s_header)) &&
        p != s_header) {
      const uint8_t *base = p - kHeaderOffset;
      if (++*matches <= 8)
        Log("RA: header copy at %p (ram %p, chip ids %s)", p, base, LooksLikeRam(base) ? "ok" : "no");
      if (!found && LooksLikeRam(base))
        found = base;
    }
    ++p;
  }
  return found;
}

// ScanRange over the runs of [begin, begin + size) that are mapped readable
// right now: arenas and views can hold reserved, lazily mapped pages, and
// touching one from this thread is a crash, not one of Drastic's faults.
const uint8_t *ScanMapped(uintptr_t begin, size_t size, int *matches, bool writable = false,
                          size_t *scanned = nullptr) {
  const uintptr_t end = begin + size;
  uintptr_t run = 0, addr = begin;
  while (addr < end) {
    MemoryInfo info{};
    u32 page = 0;
    if (R_FAILED(svcQueryMemory(&info, &page, addr)) || info.addr + info.size <= addr)
      break;
    const uintptr_t stop = info.addr + info.size < end ? info.addr + info.size : end;
    const bool readable = info.type != MemType_Unmapped && info.type != MemType_Io &&
                          (info.perm & Perm_R) && (!writable || (info.perm & Perm_W)) &&
                          !(info.attr & MemAttr_IsUncached);
    if (readable && !run)
      run = addr;
    if (run && (!readable || stop == end)) {
      const uintptr_t run_end = readable ? stop : addr;
      if (scanned)
        *scanned += run_end - run;
      if (const uint8_t *ram =
              ScanRange(reinterpret_cast<const uint8_t *>(run), run_end - run, matches))
        return ram;
      run = 0;
    }
    addr = stop;
  }
  return nullptr;
}

const uint8_t *FindRam() {
  int matches = 0;
  void *raw;
  size_t size;
  for (int i = 0; ashmem_region_at(i, &raw, &size) >= 0; i++)
    if (raw)
      if (const uint8_t *ram = ScanMapped(reinterpret_cast<uintptr_t>(raw), size, &matches))
        return ram;
  // Not in an arena: any writable memory of the process (the core's BSS, its
  // anonymous mappings, the heap). 39-bit address space.
  size_t scanned = 0;
  if (const uint8_t *ram = ScanMapped(0, 0x8000000000ULL, &matches, true, &scanned))
    return ram;
  static int s_misses;
  if (++s_misses <= 6)
    Log("RA: DS RAM not found yet (%d header copies, %zu MiB scanned)", matches, scanned >> 20);
  return nullptr;
}

// ---- rcheevos callbacks -----------------------------------------------------

// Diagnostics: what the achievement set asks for, logged by LogDiagnostics.
unsigned s_reads_ram, s_reads_dtcm, s_reads_other, s_ra_frames;
uint32_t s_refused[8];
unsigned s_refused_count;

uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *) {
  if (address < kRamSize)
    ++s_reads_ram;
  else if (address >= 0x1000000 && address < 0x1004000)
    ++s_reads_dtcm;
  else
    ++s_reads_other;
  if (s_ram && address >= 0x1000000) {
    bool seen = false;
    for (unsigned i = 0; i < s_refused_count; i++)
      seen |= s_refused[i] == address;
    if (!seen && s_refused_count < 8)
      s_refused[s_refused_count++] = address;
  }
  // The DS repeats its 4 MiB main RAM up to 0x02FFFFFF; some sets read a mirror.
  if (address >= kRamSize && address < 0x1000000)
    address &= kRamSize - 1;
  if (!s_ram || address >= kRamSize)
    return 0;
  if (num_bytes > kRamSize - address)
    num_bytes = kRamSize - address;
  std::memcpy(buffer, s_ram + address, num_bytes);
  return num_bytes;
}

size_t CurlWrite(void *data, size_t size, size_t count, void *out) {
  static_cast<std::string *>(out)->append(static_cast<char *>(data), size * count);
  return size * count;
}

const char *UserAgent() {
  static std::string agent;
  if (agent.empty()) {
    agent = "tico-drastic/1.0 (Nintendo Switch) Drastic/r2.6.0.4a";
    char clause[64] = "";
    if (rc_client_get_user_agent_clause(nullptr, clause, sizeof(clause)) > 0)
      agent += std::string(" ") + clause;
  }
  return agent.c_str();
}

void RC_CCONV ServerCall(const rc_api_request_t *request, rc_client_server_callback_t callback,
                         void *callback_data, rc_client_t *) {
  std::string url = request->url;
  std::string post = request->post_data ? request->post_data : "";
  mutexLock(&s_job_mutex);
  s_jobs.push_back([url, post, callback, callback_data] {
    std::string body;
    long status = 0;
    if (CURL *curl = curl_easy_init()) {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_USERAGENT, UserAgent());
      if (!post.empty())
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
      // devkitPro's curl has no usable CA bundle (as in the other tico cores)
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
      const CURLcode res = curl_easy_perform(curl);
      if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
      else
        Log("RA: HTTP error %s", curl_easy_strerror(res));
      curl_easy_cleanup(curl);
    }
    mutexLock(&s_done_mutex);
    s_done.push_back([body, status, callback, callback_data] {
      rc_api_server_response_t response{};
      response.body = body.c_str();
      response.body_length = body.size();
      response.http_status_code = status ? status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
      callback(&response, callback_data);
    });
    mutexUnlock(&s_done_mutex);
  });
  mutexUnlock(&s_job_mutex);
  condvarWakeOne(&s_job_cond);
}

void Worker(void *) {
  for (;;) {
    mutexLock(&s_job_mutex);
    while (s_jobs.empty() && !s_stopping)
      condvarWait(&s_job_cond, &s_job_mutex);
    if (s_jobs.empty()) { // stopping, every request sent (unlocks included)
      mutexUnlock(&s_job_mutex);
      return;
    }
    std::function<void()> job = std::move(s_jobs.front());
    s_jobs.pop_front();
    mutexUnlock(&s_job_mutex);
    job();
  }
}

void OnEvent(const rc_client_event_t *event, rc_client_t *) {
  switch (event->type) {
  case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
    Log("RA: unlocked %u %s", event->achievement->id, event->achievement->title);
    Toast(std::string("Succes debloque : ") + event->achievement->title);
    break;
  case RC_CLIENT_EVENT_GAME_COMPLETED:
    Toast("RetroAchievements : jeu termine a 100 % !");
    break;
  case RC_CLIENT_EVENT_SERVER_ERROR:
    if (event->server_error)
      Log("RA: server error %s", event->server_error->error_message);
    break;
  case RC_CLIENT_EVENT_DISCONNECTED:
    Log("RA: disconnected, unlocks are queued");
    break;
  default:
    break;
  }
}

void LoadGame() {
  rc_client_begin_identify_and_load_game(
      s_client, RC_CONSOLE_NINTENDO_DS, s_rom_path.c_str(), nullptr, 0,
      [](int result, const char *error, rc_client_t *client, void *) {
        if (result != RC_OK) {
          Log("RA: game not loaded (%d) %s", result, error ? error : "");
          if (result == RC_NO_GAME_LOADED)
            Toast("RetroAchievements : jeu non reconnu");
          return;
        }
        s_game_loaded = true;
        const rc_client_game_t *game = rc_client_get_game_info(client);
        rc_client_user_game_summary_t summary{};
        rc_client_get_user_game_summary(client, &summary);
        Log("RA: playing %u %s (%u/%u)", game->id, game->title, summary.num_unlocked_achievements,
            summary.num_core_achievements);
        Toast(std::string("RA Playing : ") + game->title + " (" +
              std::to_string(summary.num_unlocked_achievements) + "/" +
              std::to_string(summary.num_core_achievements) + ")");
      },
      nullptr);
}

// Whether the RAM changes, what the set reads, and how its achievements stand.
void LogDiagnostics() {
  uint32_t sum = 0;
  for (uint32_t off = 0; off < kRamSize; off += 64)
    sum = sum * 31 + s_ram[off];
  Log("RA diag: %u RA frames, RAM sum %08x, reads ram %u dtcm %u other %u", s_ra_frames, sum,
      s_reads_ram, s_reads_dtcm, s_reads_other);
  for (unsigned i = 0; i < s_refused_count; i++)
    Log("RA diag: refused address 0x%07x", s_refused[i]);
  s_reads_ram = s_reads_dtcm = s_reads_other = 0;

  rc_client_achievement_list_t *list = rc_client_create_achievement_list(
      s_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
  if (!list)
    return;
  unsigned states[4] = {};
  for (uint32_t b = 0; b < list->num_buckets; b++) {
    const rc_client_achievement_bucket_t &bucket = list->buckets[b];
    Log("RA diag: bucket %u '%s': %u", bucket.bucket_type, bucket.label ? bucket.label : "",
        bucket.num_achievements);
    for (uint32_t a = 0; a < bucket.num_achievements; a++) {
      const rc_client_achievement_t *ach = bucket.achievements[a];
      if (ach->state < 4)
        ++states[ach->state];
      if (bucket.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED && a < 5)
        Log("RA diag:   unsupported %u %s", ach->id, ach->title);
    }
  }
  Log("RA diag: states inactive %u active %u unlocked %u disabled %u", states[0], states[1],
      states[2], states[3]);
  rc_client_destroy_achievement_list(list);
}

} // namespace

extern "C" void tico_ra_start(const char *rom_path) {
  Log("RA: start");
  const tico::Session &session = tico::CurrentSession();
  if (!session.valid || !session.raEnabled || session.raUsername.empty() || session.raToken.empty()) {
    Log("RA: off (session %s, enabled %d)", session.valid ? "ok" : "missing", session.raEnabled);
    return;
  }
  FILE *rom = std::fopen(rom_path, "rb");
  if (!rom || std::fread(s_header, 1, sizeof(s_header), rom) != sizeof(s_header)) {
    Log("RA: cannot read the ROM header of %s", rom_path);
    if (rom)
      std::fclose(rom);
    return;
  }
  std::fclose(rom);
  s_rom_path = rom_path;

  Log("RA: ROM header read");
  // The host opens sockets only for an SMB game folder; curl needs them too.
  if (!switchStorageSocketReady()) {
    const Result rc = socketInitializeDefault();
    s_own_sockets = R_SUCCEEDED(rc);
    Log("RA: sockets 0x%x", rc);
  }
  // Stack 256 KiB on the system core, as the other tico cores' RA worker
  if (R_FAILED(threadCreate(&s_worker, Worker, nullptr, nullptr, 0x40000, 0x2C, -2)) ||
      R_FAILED(threadStart(&s_worker))) {
    Log("RA: worker thread not started");
    return;
  }
  s_worker_started = true;
  Log("RA: worker started");
  s_client = rc_client_create(ReadMemory, ServerCall);
  if (!s_client)
    return;
  rc_client_set_event_handler(s_client, OnEvent);
  // Drastic keeps save states, cheats and fast forward: Casual only.
  rc_client_set_hardcore_enabled(s_client, 0);
  // No account name in the log: people paste it into bug reports.
  Log("RA: login, ROM %s", rom_path);
  rc_client_begin_login_with_token(
      s_client, session.raUsername.c_str(), session.raToken.c_str(),
      [](int result, const char *error, rc_client_t *, void *) {
        if (result != RC_OK) {
          Log("RA: login failed (%d) %s", result, error ? error : "");
          Toast("RetroAchievements : connexion refusee");
          return;
        }
        // The game loads once the RAM is found (tico_ra_frame): loading checks
        // every address the set reads, and disables those it cannot read.
        Log("RA: logged in");
        s_logged_in = true;
      },
      nullptr);
}

extern "C" void tico_ra_frame(int running) {
  if (!s_client)
    return;
  std::deque<std::function<void()>> done;
  mutexLock(&s_done_mutex);
  done.swap(s_done);
  mutexUnlock(&s_done_mutex);
  for (auto &callback : done)
    callback();

  ++s_frames;
  // Only once logged in: without RA nothing reads Drastic's memory.
  // A whole-process scan is heavy: at 2 s, then every 5 s.
  if (s_logged_in && !s_ram && s_frames >= 120 && s_frames % 300 == 120) {
    s_ram = FindRam();
    if (s_ram) {
      Log("RA: DS RAM at %p after %u frames", s_ram, s_frames);
      LoadGame();
    }
  }
  if (running && s_ram && s_game_loaded) {
    rc_client_do_frame(s_client);
    // Diagnostics for bug reports: at 10 s, then every 30 s, six times
    ++s_ra_frames;
    if ((s_ra_frames == 600 || s_ra_frames % 1800 == 0) && s_ra_frames <= 9000)
      LogDiagnostics();
  } else {
    rc_client_idle(s_client);
  }
}

extern "C" void tico_ra_stop(void) {
  // hbloader runs the launcher in this same process: no RA thread or socket
  // session may outlive the core.
  if (s_worker_started) {
    mutexLock(&s_job_mutex);
    s_stopping = true;
    mutexUnlock(&s_job_mutex);
    condvarWakeAll(&s_job_cond);
    threadWaitForExit(&s_worker);
    threadClose(&s_worker);
    s_worker_started = false;
  }
  if (s_client) {
    rc_client_destroy(s_client);
    s_client = nullptr;
  }
  if (s_own_sockets) {
    socketExit();
    s_own_sockets = false;
  }
  Log("RA: stopped");
}
