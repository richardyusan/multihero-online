// SPDX-License-Identifier: MIT
// desktop_main.cpp - Windows and Linux backend (SDL2): window, sharp scaling, sound, keyboard,
// mouse, touch screens and up to 4 game pads (each pad its own player).
//
// Files next to the program:   assets/   the prepared game data (tools/prep_assets.py)
// Saves, settings and logs:     Windows %APPDATA%\MultiHero\   Linux ~/.local/share/MultiHero/
//                               (or ./save/ next to the program when a file "portable.txt" exists)
//
// Keys of the port: Alt+Enter = full screen on/off. Everything else is the game's own keys.
#include "platform.h"
#include "bbrt.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;        // the composited frame, 1:1
static SDL_Texture *g_big;        // the frame scaled up by an integer factor (nearest)
static int g_tex_w, g_tex_h, g_big_k;
static SDL_Rect g_dst;            // where the picture is drawn in the window
static std::string g_assets, g_data;
static FILE *g_logf;
static bool g_touch_mode;         // a touch screen was used: show the on-screen controls
static SDL_AudioDeviceID g_audio;

// ---------------------------------------------------------------- SDL scancode -> DirectInput
static int di_scancode(SDL_Scancode s) {
  if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
    static const int letters[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                    49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    return letters[s - SDL_SCANCODE_A];
  }
  if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return 2 + (s - SDL_SCANCODE_1);
  if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 59 + (s - SDL_SCANCODE_F1);
  switch (s) {
    case SDL_SCANCODE_0: return 11;
    case SDL_SCANCODE_ESCAPE: return 1;
    case SDL_SCANCODE_MINUS: return 12;
    case SDL_SCANCODE_EQUALS: return 13;
    case SDL_SCANCODE_BACKSPACE: return 14;
    case SDL_SCANCODE_TAB: return 15;
    case SDL_SCANCODE_LEFTBRACKET: return 26;
    case SDL_SCANCODE_RIGHTBRACKET: return 27;
    case SDL_SCANCODE_RETURN: return 28;
    case SDL_SCANCODE_LCTRL: return 29;
    case SDL_SCANCODE_SEMICOLON: return 39;
    case SDL_SCANCODE_APOSTROPHE: return 40;
    case SDL_SCANCODE_GRAVE: return 41;
    case SDL_SCANCODE_LSHIFT: return 42;
    case SDL_SCANCODE_BACKSLASH: return 43;
    case SDL_SCANCODE_COMMA: return 51;
    case SDL_SCANCODE_PERIOD: return 52;
    case SDL_SCANCODE_SLASH: return 53;
    case SDL_SCANCODE_RSHIFT: return 54;
    case SDL_SCANCODE_KP_MULTIPLY: return 55;
    case SDL_SCANCODE_LALT: return 56;
    case SDL_SCANCODE_SPACE: return 57;
    case SDL_SCANCODE_CAPSLOCK: return 58;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 69;
    case SDL_SCANCODE_SCROLLLOCK: return 70;
    case SDL_SCANCODE_KP_7: return 71;
    case SDL_SCANCODE_KP_8: return 72;
    case SDL_SCANCODE_KP_9: return 73;
    case SDL_SCANCODE_KP_MINUS: return 74;
    case SDL_SCANCODE_KP_4: return 75;
    case SDL_SCANCODE_KP_5: return 76;
    case SDL_SCANCODE_KP_6: return 77;
    case SDL_SCANCODE_KP_PLUS: return 78;
    case SDL_SCANCODE_KP_1: return 79;
    case SDL_SCANCODE_KP_2: return 80;
    case SDL_SCANCODE_KP_3: return 81;
    case SDL_SCANCODE_KP_0: return 82;
    case SDL_SCANCODE_KP_PERIOD: return 83;
    case SDL_SCANCODE_F11: return 87;
    case SDL_SCANCODE_F12: return 88;
    case SDL_SCANCODE_KP_ENTER: return 156;
    case SDL_SCANCODE_RCTRL: return 157;
    case SDL_SCANCODE_KP_DIVIDE: return 181;
    case SDL_SCANCODE_RALT: return 184;
    case SDL_SCANCODE_HOME: return 199;
    case SDL_SCANCODE_UP: return 200;
    case SDL_SCANCODE_PAGEUP: return 201;
    case SDL_SCANCODE_LEFT: return 203;
    case SDL_SCANCODE_RIGHT: return 205;
    case SDL_SCANCODE_END: return 207;
    case SDL_SCANCODE_DOWN: return 208;
    case SDL_SCANCODE_PAGEDOWN: return 209;
    case SDL_SCANCODE_INSERT: return 210;
    case SDL_SCANCODE_DELETE: return 211;
    default: return 0;
  }
}

// ---------------------------------------------------------------- game pads
// Layout like the Android version: A attack, B jump, X special, Y block, LB/RB throw,
// LT/RT block, START = Enter, BACK = Esc. Every pad is its own player (rt_pad).
struct Pad {
  SDL_GameController *gc = nullptr;
  bool dir[4] = {false, false, false, false};
  bool trig = false;
};
static std::map<SDL_JoystickID, Pad> g_pads;

static int pad_vb(int button) {
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return VB_SHOT;
    case SDL_CONTROLLER_BUTTON_B: return VB_JUMP;
    case SDL_CONTROLLER_BUTTON_X: return VB_SPECIAL;
    case SDL_CONTROLLER_BUTTON_Y: return VB_BLOCK;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return VB_GRAB;
  }
  return -1;
}
static void pad_dirs(SDL_JoystickID id) {
  auto it = g_pads.find(id);
  if (it == g_pads.end()) return;
  Pad &p = it->second;
  SDL_GameController *gc = p.gc;
  float x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
  float y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
  bool nd[4] = {
      y < -0.5f || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP),
      y > 0.5f || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN),
      x < -0.5f || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT),
      x > 0.5f || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)};
  static const int vbs[4] = {VB_UP, VB_DOWN, VB_LEFT, VB_RIGHT};
  for (int i = 0; i < 4; i++)
    if (nd[i] != p.dir[i]) { p.dir[i] = nd[i]; rt_pad(id, vbs[i], nd[i]); }
  bool t = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000 ||
           SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
  if (t != p.trig) { p.trig = t; rt_pad(id, VB_BLOCK, t); }
}

// ---------------------------------------------------------------- window / picture
static void toggle_fullscreen() {
  bool fs = SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
  SDL_ShowCursor(fs ? SDL_ENABLE : SDL_DISABLE);
}

static void window_to_screen(int wx, int wy, float &nx, float &ny) {
  int ow, oh, ww, wh;
  SDL_GetRendererOutputSize(g_ren, &ow, &oh);
  SDL_GetWindowSize(g_win, &ww, &wh);
  float sx = ww ? (float)ow / ww : 1, sy = wh ? (float)oh / wh : 1;   // high-dpi
  nx = g_dst.w ? (wx * sx - g_dst.x) / g_dst.w : 0;
  ny = g_dst.h ? (wy * sy - g_dst.y) / g_dst.h : 0;
}

void plat_present(const uint32_t *pix, int w, int h) {
  if (!g_tex || w != g_tex_w || h != g_tex_h) {
    if (g_tex) SDL_DestroyTexture(g_tex);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
    g_tex_w = w;
    g_tex_h = h;
    g_big_k = 0;
  }
  SDL_UpdateTexture(g_tex, nullptr, pix, w * 4);
  int ow, oh;
  SDL_GetRendererOutputSize(g_ren, &ow, &oh);
  // keep the aspect ratio, then "sharp bilinear": scale up by an integer factor with nearest
  // pixels and smooth only the last fraction
  float s = std::min((float)ow / w, (float)oh / h);
  g_dst.w = (int)(w * s);
  g_dst.h = (int)(h * s);
  g_dst.x = (ow - g_dst.w) / 2;
  g_dst.y = (oh - g_dst.h) / 2;
  int k = std::max(1, (int)s);
  if (k != g_big_k) {
    if (g_big) SDL_DestroyTexture(g_big);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    g_big = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, w * k, h * k);
    g_big_k = k;
  }
  SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
  if (g_big) {
    SDL_SetRenderTarget(g_ren, g_big);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    SDL_SetRenderTarget(g_ren, nullptr);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_big, nullptr, &g_dst);
  } else {
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, &g_dst);
  }
  SDL_RenderPresent(g_ren);
}

void plat_pump() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_QUIT:
        plat_exit(0);
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        bool down = e.type == SDL_KEYDOWN;
        if (down && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
          toggle_fullscreen();
          break;
        }
        if (down && e.key.repeat) break;
        int sc = di_scancode(e.key.keysym.scancode);
        if (sc) rt_key(sc, down);
        break;
      }
      case SDL_MOUSEMOTION: {
        if (e.motion.which == SDL_TOUCH_MOUSEID) break;
        float nx, ny;
        window_to_screen(e.motion.x, e.motion.y, nx, ny);
        rt_mouse(nx, ny, 0, false);
        break;
      }
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        if (e.button.which == SDL_TOUCH_MOUSEID) break;
        g_touch_mode = false;
        float nx, ny;
        window_to_screen(e.button.x, e.button.y, nx, ny);
        int b = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 3;
        rt_mouse(nx, ny, b, e.type == SDL_MOUSEBUTTONDOWN);
        break;
      }
      case SDL_FINGERDOWN:
      case SDL_FINGERMOTION:
      case SDL_FINGERUP: {
        // touch screens (Windows tablets): on-screen controls like on a phone
        g_touch_mode = true;
        int ww, wh;
        SDL_GetWindowSize(g_win, &ww, &wh);
        float nx, ny;
        window_to_screen((int)(e.tfinger.x * ww), (int)(e.tfinger.y * wh), nx, ny);
        int act = e.type == SDL_FINGERDOWN ? 0 : e.type == SDL_FINGERMOTION ? 1 : 2;
        rt_touch((int)(e.tfinger.fingerId & 0xffff), nx, ny, act);
        break;
      }
      case SDL_CONTROLLERDEVICEADDED: {
        SDL_GameController *gc = SDL_GameControllerOpen(e.cdevice.which);
        if (gc) {
          SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
          g_pads[id].gc = gc;
          char buf[160];
          snprintf(buf, sizeof buf, "game pad connected: %s", SDL_GameControllerName(gc));
          plat_log(buf);
        }
        break;
      }
      case SDL_CONTROLLERDEVICEREMOVED: {
        auto it = g_pads.find(e.cdevice.which);
        if (it != g_pads.end()) {
          for (int vb = 0; vb < VB_COUNT; vb++) rt_pad(e.cdevice.which, vb, false);
          SDL_GameControllerClose(it->second.gc);
          g_pads.erase(it);
        }
        break;
      }
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
        int b = e.cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_START) { rt_key(28, down); break; }
        if (b == SDL_CONTROLLER_BUTTON_BACK) { rt_key(1, down); break; }
        int vb = pad_vb(b);
        if (vb >= 0) { if (rt_pad(e.cbutton.which, vb, down) == 1 && down) g_touch_mode = false; }
        else pad_dirs(e.cbutton.which);
        break;
      }
      case SDL_CONTROLLERAXISMOTION:
        pad_dirs(e.caxis.which);
        break;
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          // release everything held so nothing sticks when the window comes back
          const int keys[] = {1, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 28, 29, 30, 31, 32, 33, 34, 35, 36,
                              37, 38, 42, 44, 45, 46, 47, 48, 49, 50, 54, 56, 57, 157, 184, 200, 203, 205, 208};
          for (int k : keys) rt_key(k, false);
          rt_touch_cancel_all();
        }
        break;
    }
  }
}

int64_t plat_ms() { return (int64_t)SDL_GetTicks64(); }
void plat_sleep(int ms) { if (ms > 0) SDL_Delay((Uint32)ms); }

bool plat_asset_read(const std::string &path, std::vector<uint8_t> &out) {
  FILE *f = fopen((g_assets + path).c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? n : 0);
  bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}
std::string plat_data_dir() { return g_data; }
void plat_log(const char *m) {
  fprintf(stderr, "[mh] %s\n", m);
  if (g_logf) { fprintf(g_logf, "%s\n", m); fflush(g_logf); }
}
[[noreturn]] void plat_exit(int code) {
  if (g_audio) SDL_CloseAudioDevice(g_audio);
  SDL_Quit();
  exit(code);
}
float plat_screen_aspect() {
  if (!g_touch_mode) return 640.0f / 480.0f;   // keyboard / pad: the plain 4:3 picture
  int w = 4, h = 3;
  SDL_GetWindowSize(g_win, &w, &h);
  return h > 0 ? (float)w / h : 4.0f / 3.0f;
}
bool plat_touch_overlay() { return g_touch_mode; }

static void SDLCALL audio_cb(void *, Uint8 *stream, int len) {
  rt_audio_mix((int16_t *)stream, len / 4);
}

static bool file_exists(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (f) fclose(f);
  return f != nullptr;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  char *base = SDL_GetBasePath();
  std::string dir = base ? base : "./";
  SDL_free(base);
  g_assets = dir + "assets/";
  if (const char *a = getenv("MH_ASSETS")) g_assets = std::string(a) + "/";
  if (file_exists(dir + "portable.txt")) {
    g_data = dir + "save/";
  } else {
    char *pref = SDL_GetPrefPath(nullptr, "MultiHero");
    g_data = pref ? pref : dir + "save/";
    SDL_free(pref);
  }
  if (const char *d = getenv("MH_DATA")) g_data = std::string(d) + "/";
  MH_MKDIR(g_data.c_str(), 0777);
  g_logf = fopen((g_data + "multihero.log").c_str(), "w");

  if (!file_exists(g_assets + "manifest.txt")) {
    std::string msg = "The game data was not found.\n\nExpected the folder:\n" + g_assets +
                      "\n\nKeep the 'assets' folder next to the program.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MultiHero", msg.c_str(), nullptr);
    return 1;
  }

  g_win = SDL_CreateWindow("MultiHero", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!g_win) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MultiHero", SDL_GetError(), nullptr);
    return 1;
  }
  SDL_SetWindowMinimumSize(g_win, 640, 480);
  g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
  if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
  if (!g_ren) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MultiHero", SDL_GetError(), g_win);
    return 1;
  }
  SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
  SDL_RenderClear(g_ren);
  SDL_RenderPresent(g_ren);

  SDL_AudioSpec want = {}, have = {};
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = audio_cb;
  g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
  else plat_log("no audio device");

  char buf[256];
  snprintf(buf, sizeof buf, "MultiHero desktop (%s), data: %s", SDL_GetPlatform(), g_data.c_str());
  plat_log(buf);
  rt_init();
  bb_main();
  plat_exit(0);
}
