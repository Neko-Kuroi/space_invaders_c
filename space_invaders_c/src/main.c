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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const char *smoke_env = getenv("SI_SMOKE_FRAMES");
    int smoke_frames = smoke_env ? atoi(smoke_env) : -1;

    srand((unsigned)(SDL_GetTicks() ^ time(NULL)));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if ((IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG)) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }
    /* 原典の mixer.pre_init(44100, -16, 1, 4096) に合わせる */
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 1, 4096) != 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Space Invaders", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "window failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    /* 原典の SCALED 相当: 論理画面を 800x600 に固定する */
    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

    char *base = SDL_GetBasePath();
    Assets assets;
    if (!assets_load(&assets, renderer, base ? base : "./")) {
        if (base) SDL_free(base);
        return 1;
    }
    if (base) SDL_free(base);

    Game game;
    game_init(&game, &assets);

    int frame = 0;
    int running = 1;
    while (running) {
        Uint32 frame_start = SDL_GetTicks();
        Input in;
        build_input(&in, smoke_frames >= 0 ? frame : -1);
        running = game_update(&game, &in, frame_start);
        game_render(&game, renderer);
        frame++;

        if (smoke_frames >= 0 && frame >= smoke_frames) {
            printf("smoke: frames=%d state=%d score=%d enemies=%d round=%d lives=%d\n",
                   frame, (int)game.state, game.score, game.ledger.count,
                   game.round_num, game.lives_spare);
            break;
        }

        /* 原典の clock.tick(60) 相当 */
        Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 16)
            SDL_Delay(16 - elapsed);
    }

    assets_free(&assets);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
