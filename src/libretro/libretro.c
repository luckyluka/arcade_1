/* ARCADE-1 — Milestone 06: Libretro adapter.
 *
 * Thin adapter between the ARCADE-1 machine (src/arcade1/machine.c) and the
 * Libretro frontend. All machine logic lives in the machine; this file only
 * translates libretro callbacks <-> the arcade1_* API. The machine itself is
 * usable without RetroArch (see tests/test_machine.c).
 *
 * Chain:
 *   RetroArch -> libretro callbacks -> arcade1_* -> machine -> back out
 */

#include "libretro.h"

#include "machine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static retro_environment_t   env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t  audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t    input_poll_cb;
static retro_input_state_t   input_state_cb;

/* The single machine instance driven by this core. Static: it is large (holds
 * the framebuffer) and there is exactly one per core load. */
static arcade1_machine_t machine;

/* --- RetroPad -> arcade1_input_t ----------------------------------------- */

/* Read the frontend's RetroPad state for port 0 into the machine's abstract
 * input. The frontend owns physical devices; we only translate. */
static void poll_input(void)
{
    arcade1_input_t in;

    if (input_poll_cb)
        input_poll_cb();

    if (!input_state_cb) {
        arcade1_set_input(&machine, &(arcade1_input_t){0});
        return;
    }

    in.up     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    in.down   = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    in.left   = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    in.right  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    in.a      = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    in.b      = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    in.start  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
    in.select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);

    arcade1_set_input(&machine, &in);
}

/* --- Lifecycle ----------------------------------------------------------- */

/* Required by the libretro ABI: which API revision this core implements.
 * RetroArch refuses to load a core that does not export it (found the hard
 * way at M15: content loading fatals without it, while the no-content menu
 * path happens to tolerate its absence). */
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_environment(retro_environment_t cb)
{
    env_cb = cb;

    /* Allow the core to run with no content loaded. */
    bool no_game = true;
    if (env_cb)
        env_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb   = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)   { audio_cb   = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb)       { input_poll_cb  = cb; }
void retro_set_input_state(retro_input_state_t cb)     { input_state_cb = cb; }

void retro_init(void)
{
    arcade1_init(&machine);
}

void retro_deinit(void)
{
    /* Nothing to release; the machine owns no dynamic resources. */
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

/* --- Descriptors --------------------------------------------------------- */

void retro_get_system_info(struct retro_system_info *info)
{
    info->library_name     = "ARCADE-1";
    info->library_version  = "0.1.0";
    info->valid_extensions  = NULL;   /* Accept any content file. */
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width   = ARCADE1_WIDTH;
    info->geometry.base_height  = ARCADE1_HEIGHT;
    info->geometry.max_width    = ARCADE1_WIDTH;
    info->geometry.max_height   = ARCADE1_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;

    info->timing.fps            = ARCADE1_FPS;
    info->timing.sample_rate    = ARCADE1_SAMPLE_RATE;
}

/* --- Emulation loop ------------------------------------------------------ */

void retro_reset(void)
{
    arcade1_reset(&machine);
}

void retro_run(void)
{
    /* input -> machine -> video + audio, all via the machine API. */
    poll_input();
    arcade1_run_frame(&machine);

    if (video_cb)
        video_cb(arcade1_framebuffer(&machine), ARCADE1_WIDTH, ARCADE1_HEIGHT,
                 arcade1_framebuffer_pitch(&machine));

    if (audio_batch_cb)
        audio_batch_cb(arcade1_audio(&machine),
                       arcade1_audio_frames(&machine));
}

/* --- Save state (unsupported) -------------------------------------------- */

size_t retro_serialize_size(void)             { return 0; }
bool   retro_serialize(void *data, size_t len)
{
    (void)data; (void)len;
    return false;
}
bool   retro_unserialize(const void *data, size_t len)
{
    (void)data; (void)len;
    return false;
}

/* --- Cheats (unsupported) ------------------------------------------------ */

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index; (void)enabled; (void)code;
}

/* --- Content loading ----------------------------------------------------- */

/* Tell the frontend the RetroPad layout so it can label controls. */
static void setup_input_descriptors(void)
{
    static const struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Select" },
        { 0, 0, 0, 0, NULL },   /* End of list. */
    };

    if (env_cb)
        env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)desc);
}

bool retro_load_game(const struct retro_game_info *game)
{
    /* The machine renders RGB565; negotiate that format with the frontend. */
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (env_cb && !env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
        return false;

    setup_input_descriptors();

    arcade1_init(&machine);
    arcade1_reset(&machine);

    /* With no content (SET_SUPPORT_NO_GAME) the machine boots into its
     * built-in demo. With content (milestone 15) the file is the guest ROM:
     * load it into the 4 KiB ROM and boot the CPU from the reset vector
     * (docs.md M15 "Firmware and frame protocol"). */
    if (game && game->data && game->size > 0) {
        if (game->size > ARCADE1_ROM_SIZE)
            return false;   /* content larger than the ROM */
        if (arcade1_load_rom(&machine.bus, game->data,
                             (uint32_t)game->size) != game->size)
            return false;
        arcade1_set_firmware(&machine, true);
    }

    return true;
}

bool retro_load_game_special(unsigned type,
                             const struct retro_game_info *info,
                             size_t num_info)
{
    (void)type; (void)info; (void)num_info;
    return false;
}

void retro_unload_game(void) {}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* --- Memory -------------------------------------------------------------- */

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
