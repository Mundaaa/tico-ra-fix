# Applies the session_override fallback to a tico core checkout (cwd).
import pathlib, sys

def sub(path, pairs):
    p = pathlib.Path(path)
    t = p.read_text(encoding='utf-8')
    crlf = '\r\n' in t
    for a, b in pairs:
        if crlf:
            a, b = a.replace('\n', '\r\n'), b.replace('\n', '\r\n')
        if b in t:
            continue  # already patched
        assert t.count(a) == 1, (path, a[:60])
        t = t.replace(a, b)
    p.write_text(t, encoding='utf-8')
    print('patched', path)

session = [
("""    if (!secure::ReadSession(text))
      return s;""",
"""    // A self-built core cannot open tico's sealed session (no build secret):
    // fall back to the player's own plain file with the same fields.
    if (!secure::ReadSession(text)) {
      FILE *file = std::fopen("sdmc:/tico/session_override.json", "rb");
      if (!file)
        return s;
      char buffer[4096];
      std::size_t n;
      while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        text.append(buffer, n);
      std::fclose(file);
    }"""),
("  std::string raUsername, raToken;\n", "  std::string raUsername, raToken;\n  std::string raPassword; ///< Only ever from session_override.json\n"),
('    s.raToken = str("ra_token");\n', '    s.raToken = str("ra_token");\n    s.raPassword = str("ra_password");\n'),
]
core = [
("""    m_raEnabled = session.valid && session.raEnabled && !session.raToken.empty();
    m_raUsername = session.raUsername;
    m_raToken = session.raToken;
    m_raPassword.clear();""",
"""    m_raEnabled = session.valid && session.raEnabled &&
        (!session.raToken.empty() || !session.raPassword.empty());
    m_raUsername = session.raUsername;
    m_raToken = session.raToken;
    m_raPassword = session.raPassword;"""),
]

found = list(pathlib.Path('.').glob('**/tico/**/TicoSession.h')) + list(pathlib.Path('.').glob('tico/TicoSession.h'))
found = sorted({f for f in found if '.git' not in f.parts})
assert found, 'no TicoSession.h'
for f in found:
    sub(f, session)
if pathlib.Path('tico/TicoCore.cpp').exists() and 'm_raPassword.clear();' in pathlib.Path('tico/TicoCore.cpp').read_text(encoding='utf-8'):
    sub('tico/TicoCore.cpp', core)

# Dolphin: with RA on, the game's hash is computed at boot (1-2 s from the SD),
# which pushes slow-starting games (Mario Kart Wii...) past the 12 s "no picture"
# watchdog, which then sends them back to tico. Give them 30 s.
dolphin_main = pathlib.Path('Source/Core/DolphinNX/main.cpp')
if dolphin_main.exists():
    sub(dolphin_main, [
        ("startup_started_at > std::chrono::seconds(12))",
         "startup_started_at > std::chrono::seconds(30))"),
        ('still at 0 presents after 12s', 'still at 0 presents after 30s'),
    ])
