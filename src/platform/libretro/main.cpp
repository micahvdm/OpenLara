#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glsym/glsym.h>
#include <libretro.h>

#include "../../game.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
struct retro_hw_render_callback hw_render;

#if defined(HAVE_PSGL)
#define RARCH_GL_FRAMEBUFFER GL_FRAMEBUFFER_OES
#define RARCH_GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_OES
#define RARCH_GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#elif defined(OSX_PPC)
#define RARCH_GL_FRAMEBUFFER GL_FRAMEBUFFER_EXT
#define RARCH_GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_EXT
#define RARCH_GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#else
#define RARCH_GL_FRAMEBUFFER GL_FRAMEBUFFER
#define RARCH_GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE
#define RARCH_GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0
#endif

#define BASE_WIDTH 320
#define BASE_HEIGHT 240

static unsigned MAX_WIDTH  = 320;
static unsigned MAX_HEIGHT = 240;

#define SND_FRAME_SIZE  4

static unsigned FRAMERATE     = 60;
static unsigned SND_RATE      = 44100;

static unsigned width         = BASE_WIDTH;
static unsigned height        = BASE_HEIGHT;

Sound::Frame *sndData;

char levelpath[255];
char basedir[1024];

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_set_rumble_state_t set_rumble_cb;

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

// multi-threading
void* osMutexInit() {
    CRITICAL_SECTION *CS = new CRITICAL_SECTION();
    InitializeCriticalSection(CS);
    return CS;
}

void osMutexFree(void *obj) {
    DeleteCriticalSection((CRITICAL_SECTION*)obj);
    delete (CRITICAL_SECTION*)obj;
}

void osMutexLock(void *obj) {
    EnterCriticalSection((CRITICAL_SECTION*)obj);
}

void osMutexUnlock(void *obj) {
    LeaveCriticalSection((CRITICAL_SECTION*)obj);
}

int osGetTime() {
    LARGE_INTEGER Freq, Count;
    QueryPerformanceFrequency(&Freq);
    QueryPerformanceCounter(&Count);
    return int(Count.QuadPart * 1000L / Freq.QuadPart);
}

void* osRWLockInit() {
   return osMutexInit();
}

void osRWLockFree(void *obj) {
   osMutexFree(obj);
}

void osRWLockRead(void *obj) {
   osMutexLock(obj);
}

void osRWUnlockRead(void *obj) {
   osMutexUnlock(obj);
}

void osRWUnlockWrite(void *obj) {
   osMutexUnlock(obj);
}


void osRWLockWrite(void *obj) {
   osMutexUnlock(obj);
}

#elif defined(__linux__)
#include <pthread.h>
#include <sys/time.h>
unsigned int startTime;

int osGetTime(void)
{
    timeval t;
    gettimeofday(&t, NULL);
    return int((t.tv_sec - startTime) * 1000 + t.tv_usec / 1000);
}
#endif

#if defined(__MACH__)
#include <mach/mach_time.h>

int osGetTime(void)
{
    const int64_t kOneMillion = 1000 * 1000;
    static mach_timebase_info_data_t info;

    if (info.denom == 0)
        mach_timebase_info(&info);

    return (int)((mach_absolute_time() * info.numer) / (kOneMillion * info.denom));
}
#endif

void osJoyVibrate(int index, float L, float R) 
{
    if(set_rumble_cb)
    {
        uint16_t left  = int(0xffffff * max(1.0f, L));
        uint16_t right = int(0xffffff * max(1.0f, R));
        set_rumble_cb(index, RETRO_RUMBLE_STRONG, left);
        set_rumble_cb(index, RETRO_RUMBLE_WEAK, right);
    }
}

void retro_init(void)
{
   contentDir[0] = cacheDir[0] = saveDir[0] = 0;
   
   const char *sysdir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir))
   {
#ifdef _WIN32
      char slash = '\\';
#else
      char slash = '/';
#endif
      sprintf(cacheDir, "%s%copenlara-", sysdir, slash);
   }
   
   	struct retro_rumble_interface rumbleInterface;
    if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumbleInterface)) 
    {
        set_rumble_cb = rumbleInterface.set_rumble_state;
    } 
}

void retro_deinit(void)
{
   contentDir[0] = cacheDir[0] = 0;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "OpenLara";
   info->library_version  = "v1";
   info->need_fullpath    = true;
   info->valid_extensions = "phd|psx|tr2"; // Anything is fine, we don't care.
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing = (struct retro_system_timing) {
      .fps = (float)FRAMERATE,
      .sample_rate = (float)SND_RATE,
   };

   info->geometry = (struct retro_game_geometry) {
      .base_width   = BASE_WIDTH,
      .base_height  = BASE_HEIGHT,
      .max_width    = MAX_WIDTH,
      .max_height   = MAX_HEIGHT,
      .aspect_ratio = 4.0 / 3.0,
   };
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      {
         "openlara_framerate",
         "Framerate (restart); 60fps|90fps|120fps|144fps|30fps",
      },
      {
         "openlara_resolution",
         "Internal resolution (restart); 320x240|360x480|480x272|512x384|512x512|640x240|640x448|640x480|720x576|800x600|960x720|1024x768|1024x1024|1280x720|1280x960|1600x1200|1920x1080|1920x1440|1920x1600|2048x2048|2560x1440|3840x2160|7680x4320|15360x8640|16000x9000",
      },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static void update_variables(bool first_startup)
{
   if (first_startup)
   {
      struct retro_variable var;

      var.key = "openlara_resolution";

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         char *pch;
         char str[100];
         snprintf(str, sizeof(str), "%s", var.value);

         pch = strtok(str, "x");
         if (pch)
            width = strtoul(pch, NULL, 0);
         pch = strtok(NULL, "x");
         if (pch)
            height = strtoul(pch, NULL, 0);

	 MAX_WIDTH  = width;
	 MAX_HEIGHT = height;

         fprintf(stderr, "[openlara]: Got size: %u x %u.\n", width, height);
      }

      var.key = "openlara_framerate";

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "30fps"))
            FRAMERATE     = 30;
         else if (!strcmp(var.value, "60fps"))
            FRAMERATE     = 60;
         else if (!strcmp(var.value, "90fps"))
            FRAMERATE     = 90;
         else if (!strcmp(var.value, "120fps"))
            FRAMERATE     = 120;
         else if (!strcmp(var.value, "144fps"))
            FRAMERATE     = 120;
      }
      else
         FRAMERATE     = 60;
   }
}

static float DeadZone(float x)
{
   return x = fabsf(x) < 0.2f ? 0.0f : x;
}

void retro_run(void)
{
   unsigned i;
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables(false);

   input_poll_cb();

   /* Start */
   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
      Input::setDown(InputKey::ikEnter, true, 0);
   else
      Input::setDown(InputKey::ikEnter, false, 0);

   /* Player 1 */
   for (i = 0; i < 2; i++)
   {
#if 0
      int lsx = input_state_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
      int lsy = input_state_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
      int rsx = input_state_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
      int rsy = input_state_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

      Input::setJoyPos(i, jkL, vec2(DeadZone(lsx), DeadZone(lsy)));
      Input::setJoyPos(i, jkR, vec2(DeadZone(rsx), DeadZone(rsy)));
#endif

      /* Up */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
         Input::setJoyDown(i, JoyKey::jkUp, true);
      else
         Input::setJoyDown(i, JoyKey::jkUp, false);

      /* Down */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
         Input::setJoyDown(i, JoyKey::jkDown, true);
      else
         Input::setJoyDown(i, JoyKey::jkDown, false);

      /* Left */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
         Input::setJoyDown(i, JoyKey::jkLeft, true);
      else
         Input::setJoyDown(i, JoyKey::jkLeft, false);

      /* Right */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
         Input::setJoyDown(i, JoyKey::jkRight, true);
      else
         Input::setJoyDown(i, JoyKey::jkRight, false);

      /* Inventory screen */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))
         Input::setJoyDown(i, JoyKey::jkSelect, true);
      else
         Input::setJoyDown(i, JoyKey::jkSelect, false);

      /* Draw weapon */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
         Input::setJoyDown(i, JoyKey::jkY, true);
      else
         Input::setJoyDown(i, JoyKey::jkY, false);

      /* Grab/shoot - Action button */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
         Input::setJoyDown(i, JoyKey::jkA, true);
      else
         Input::setJoyDown(i, JoyKey::jkA, false);

      /* Roll */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
         Input::setJoyDown(i, JoyKey::jkB, true);
      else
         Input::setJoyDown(i, JoyKey::jkB, false);

      /* Jump */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
         Input::setJoyDown(i, JoyKey::jkX, true);
      else
         Input::setJoyDown(i, JoyKey::jkX, false);

      /* Walk */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
         Input::setJoyDown(i, JoyKey::jkRB, true);
      else
         Input::setJoyDown(i, JoyKey::jkRB, false);

      /* Look */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))
         Input::setJoyDown(i, JoyKey::jkLB, true);
      else
         Input::setJoyDown(i, JoyKey::jkLB, false);

      /* Duck/Crouch */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2))
         Input::setJoyDown(i, JoyKey::jkLT, true);
      else
         Input::setJoyDown(i, JoyKey::jkLT, false);

      /* Dash */
      if (input_state_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))
         Input::setJoyDown(i, JoyKey::jkRT, true);
      else
         Input::setJoyDown(i, JoyKey::jkRT, false);
   }

   int audio_frames = SND_RATE / FRAMERATE;
   int16_t *samples = (int16_t*)sndData;

   Sound::fill(sndData, audio_frames);

   while (audio_frames > 512)
   {
      audio_batch_cb(samples, 512);
      samples += 1024;
      audio_frames -= 512;
   }
   audio_batch_cb(samples, audio_frames);

   Core::deltaTime             = 1.0 / FRAMERATE;
   Core::settings.detail.vsync = false;

   updated = Game::update();
   if (updated)
      Game::render();
   video_cb(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
}

static void context_reset(void)
{
   char musicpath[255];
#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif
   fprintf(stderr, "Context reset!\n");
   rglgen_resolve_symbols(hw_render.get_proc_address);

   sndData = new Sound::Frame[SND_RATE * 2 * sizeof(int16_t) / FRAMERATE];
   snprintf(musicpath, sizeof(musicpath), "%s%c05.ogg",
         basedir, slash);
   Game::init(levelpath/*, musicpath */);
}

static void context_destroy(void)
{
   fprintf(stderr, "Context destroy!\n");
   delete[] sndData;
   Game::deinit();
}

#ifdef HAVE_OPENGLES
static bool retro_init_hw_context(void)
{
#if defined(HAVE_OPENGLES_3_1)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES_VERSION;
   hw_render.version_major = 3;
   hw_render.version_minor = 1;
#elif defined(HAVE_OPENGLES3)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#endif
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.depth = true;
   hw_render.stencil = true;
   hw_render.bottom_left_origin = true;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   return true;
}
#else
static bool retro_init_hw_context(void)
{
#if defined(CORE)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
   hw_render.version_major = 3;
   hw_render.version_minor = 1;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.depth = true;
   hw_render.stencil = true;
   hw_render.bottom_left_origin = true;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   return true;
}
#endif

static void extract_directory(char *buf, const char *path, size_t size)
{
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   char *base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
   {
      buf[0] = '.';
      buf[1] = '\0';
   }
}

bool retro_load_game(const struct retro_game_info *info)
{
   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Inventory" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Jump" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Draw weapon" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Action (Shoot/grab)" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Roll" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Look (when holding)" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Walk (when holding)" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Duck/Crouch (TR3 and up)" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Dash (TR3 and up)" },
      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT, NULL);

   update_variables(true);

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "XRGB8888 is not supported.\n");
      return false;
   }

   if (!retro_init_hw_context())
   {
      fprintf(stderr, "HW Context could not be initialized, exiting...\n");
      return false;
   }

   fprintf(stderr, "Loaded game!\n");
   (void)info;


   levelpath[0] = '\0';
   strcpy(levelpath, info->path);

   basedir[0] = '\0';
   extract_directory(basedir, info->path, sizeof(basedir));

   Core::width  = width;
   Core::height = height;

   return true;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_reset(void)
{}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

void osToggleVR(bool enable) {
}
