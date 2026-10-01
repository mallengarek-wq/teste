from pathlib import Path

path = Path(__file__).with_name("main.c")
text = path.read_text(encoding="utf-8")

old = '''            if (g_game_state == 2) {
                rc_client_do_frame(g_client);
                if (++frame_counter >= 600) {
                    frame_counter = 0;
                    write_line("RA RUNTIME: active");
                }
            }
'''

new = '''            /* rcheevos queues the final game activation onto rc_client_do_frame()
               when background memory reads are disabled. Pump the client while the
               user is logged in, including while g_game_state == 1, otherwise the
               load can remain in CARREGANDO forever waiting for a frame callback. */
            if (g_login_state == 2) {
                rc_client_do_frame(g_client);
            }

            if (g_game_state == 2) {
                if (++frame_counter >= 600) {
                    frame_counter = 0;
                    write_line("RA RUNTIME: active");
                }
            }
'''

if old not in text:
    raise SystemExit("Expected rc_client_do_frame block not found; refusing to patch an unknown source version")

text = text.replace(old, new, 1)
text = text.replace("RA-PSP PPSSPP v0.6.4 SYNC FIX", "RA-PSP PPSSPP v0.7.1 LOAD FIX")
text = text.replace("RA-PSP v0.6.4:", "RA-PSP v0.7.1:")

path.write_text(text, encoding="utf-8")
print("Applied rcheevos load-state frame pump fix to", path)
