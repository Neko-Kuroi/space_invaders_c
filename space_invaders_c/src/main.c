/*
 * main.c - SDL の初期化、イベントの読み取り、メインループ。
 * ゲームの規則は game.c 側にあり、ここは Input への変換だけを行う。
 *
 * スモークテスト用: 環境変数 SI_SMOKE_FRAMES=N を付けると、入力を合成して
 * N フレームだけ回し、状態を表示して終了する（画面なし環境での確認用）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include "assets.h"
#include "game.h"

static void build_input(Input *in, int smoke_frame)
{
    memset(in, 0, sizeof *in);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            in->quit = true;
        if (e.type == SDL_KEYUP) {
            in->any_keyup = true;
            if (e.key.keysym.sym == SDLK_ESCAPE)
                in->quit = true;
            if (e.key.keysym.sym == SDLK_a || e.key.keysym.sym == SDLK_d)
                in->keyup_move = true;
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.sym == SDLK_s)
                in->fire_keydown = true;
        }
    }
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    in->held_a = keys[SDL_SCANCODE_A] != 0;
    in->held_d = keys[SDL_SCANCODE_D] != 0;

    if (smoke_frame >= 0) {
        /* 合成入力: 2フレーム目に開始、右→左に動き、25フレームごとに撃つ */
        if (smoke_frame == 2)
            in->any_keyup = true;
        if (smoke_frame > 2) {
            int phase = (smoke_frame / 60) % 2;
            in->held_d = phase == 0;
            in->held_a = phase == 1;
            if (smoke_frame % 25 == 0)
                in->fire_keydown = true;
        }
    }
}

/* 目標は 60fps。フレームの長さを整数ミリ秒に丸めると 62.5fps になって
 * フレーム単位で動く量（弾の速さなど）がわずかに速くなるため、
 * 高精度タイマーで「次のフレームの開始時刻」を積み上げて待つ。 */
static void wait_for_next_frame(Uint64 *next, Uint64 freq)
{
    *next += freq / 60;
    Uint64 t = SDL_GetPerformanceCounter();
    if (*next > t) {
        Uint32 ms = (Uint32)((*next - t) * 1000 / freq);
        if (ms > 0)
            SDL_Delay(ms);
    } else if (t - *next > freq / 10) {
        /* 大きく遅れたら（ウィンドウ移動など）追いつこうとせず仕切り直す */
        *next = t;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const char *smoke_env = getenv("SI_SMOKE_FRAMES");
    int smoke_frames = smoke_env ? atoi(smoke_env) : -1;

    srand((unsigned)(SDL_GetTicks() ^ time(NULL)));

    int status = 1;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    Assets assets;
    bool assets_ready = false;
    bool img_ready = false, ttf_ready = false, audio_ready = false;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if ((IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG)) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        goto done;
    }
    img_ready = true;
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        goto done;
    }
    ttf_ready = true;

    /* 音は無くても遊べるので、音声デバイスが使えなくても続行する
     * （WSL・CI・音声デバイスのない環境など）。
     * 原典の mixer.pre_init(44100, -16, 1, 4096) に合わせる。 */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 &&
        Mix_OpenAudio(44100, AUDIO_S16SYS, 1, 4096) == 0) {
        audio_ready = true;
        /* チャンネル0をミステリーシップ出現音の専用に予約する（game.h） */
        Mix_ReserveChannels(1);
    } else {
        fprintf(stderr, "audio unavailable, continuing without sound: %s\n",
                SDL_GetError()[0] ? SDL_GetError() : Mix_GetError());
    }

    window = SDL_CreateWindow(
        "Space Invaders", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "window failed: %s\n", SDL_GetError());
        goto done;
    }
    renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        goto done;
    }
    /* 原典の SCALED 相当: 論理画面を 800x600 に固定する */
    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

    char *base = SDL_GetBasePath();
    assets_ready = assets_load(&assets, renderer, base ? base : "./", audio_ready);
    if (base)
        SDL_free(base);
    if (!assets_ready)
        goto done;

    Game game;
    game_init(&game, &assets);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next_frame = SDL_GetPerformanceCounter();
    int frame = 0;
    int running = 1;
    while (running) {
        /* 更新と描画で同じ時刻を使う（爆発の経過時間などがずれないように） */
        Uint32 now = SDL_GetTicks();
        Input in;
        build_input(&in, smoke_frames >= 0 ? frame : -1);
        running = game_update(&game, &in, now);
        game_render(&game, renderer, now);
        frame++;

        if (smoke_frames >= 0 && frame >= smoke_frames) {
            printf("smoke: frames=%d state=%d score=%d enemies=%d round=%d lives=%d\n",
                   frame, (int)game.state, game.score, game.ledger.count,
                   game.round_num, game.lives_spare);
            break;
        }

        wait_for_next_frame(&next_frame, freq);
    }
    status = 0;

done:
    if (assets_ready)
        assets_free(&assets);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    if (audio_ready)
        Mix_CloseAudio();
    if (ttf_ready)
        TTF_Quit();
    if (img_ready)
        IMG_Quit();
    SDL_Quit();
    return status;
}
