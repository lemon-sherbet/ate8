#include <assert.h>
#include <stdio.h>
#include <SDL2/SDL.h>

typedef unsigned char UCHAR;

enum {
  W = 64, H = 32,
  CYCLES_PER_FRAME = 10,
  BLACK = 0
};
const Uint32 WHITE = 0xFFFFFFFFLU;

Uint16 PC = 0x200,
       SP = 0xEFF,
       I = 0;
Uint8 V[16];
Uint8 M[0x1000] = {
    //font
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F 
};
UCHAR keys[16];
int delay = 0, sound = 0;

//64*32; 32 bit surface; pixels either BLACK or WHITE
SDL_Surface* screen = NULL;

#if DEBUGPRINT
#define debugprintf(...) fprintf(stderr, __VA_ARGS__)
#else
#define debugprintf(...) 1
#endif

//mem read
static inline Uint8 Rd(Uint16 addr) { return M[addr & 0xFFF]; }
//mem write
static inline void  Wr(Uint16 addr, Uint8 val) { M[addr & 0xFFF] = val; }

static void DrawSpr(Uint8 X, Uint8 Y, UCHAR n) {
  V[0xF] = 0;
  //go row by row
  for (short i = 0; i < n; i++) {
    UCHAR row = Rd(I+i);
    int x = X;
    int y = Y+i;
    for (short bit = 0x80; bit != 0; bit >>= 1, x += 1) {
      short pix = row & bit;
      if (pix && x >= 0 && x < W && y >= 0 && y < H) {
        Uint32* p = (Uint32*)((char*)screen->pixels + (x*screen->format->BytesPerPixel+y*screen->pitch));
        if (*p == BLACK) *p = WHITE;
        else if (*p == WHITE) *p = BLACK, V[0xF] = 1;
      }
    }
  }
}

static void WaitKey(Uint8* dst) {
  extern int keymap[16];
  SDL_Event ev;
  int key;
  for (;;) {
    if (SDL_WaitEvent(&ev)) {
      if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        for (int i = 0; i < sizeof keymap / sizeof *keymap; i++) {
          if (keymap[i] == ev.key.keysym.scancode) {
            key = i;
            goto pressed;
          }
        }
      } else if (ev.type == SDL_QUIT) exit(0);
    }
  }
  pressed:
  *dst = key;
}

static void PutBCD(Uint8 x) {
  Wr(I,   (x/100) % 10);
  Wr(I+1,  (x/10) % 10);
  Wr(I+2,       x % 10);
}

static inline int _matchX(char p, UCHAR val) {
  UCHAR b;
  if (p >= '0' && p <= '9') b = p - '0';
  else if (p >= 'A' && p <= 'F') b = 0xA + p - 'A';
  else return 1;

  return b == val;
}

#define LIST_INSTRS(i)                                                          \
  i("00E0",    "CLR",  SDL_FillRect(screen, NULL, BLACK)                       )\
  i("00EE",    "RET",  SP += 2; PC = (Rd(SP) << 8) | Rd(SP+1)                  )\
  i("1nnn",    "JMP",  PC = NNN                                                )\
  i("2nnn",   "CALL",  Wr(SP, PC >> 8), Wr(SP+1, PC & 0xFF), SP -= 2, PC = NNN )\
  i("3xnn" , "IFEQI",  if (Vx == NN) PC += 2;                                  )\
  i("4xnn", "IFNEQI",  if (Vx != NN) PC += 2;                                  )\
  i("5xy0",   "IFEQ",  if (Vx == Vy) PC += 2;                                  )\
  i("6xnn",   "MOVI",  Vx  = NN                                                )\
  i("7xnn",   "ADDI",  Vx += NN                                                )\
  i("8xy0",    "MOV",  Vx  = Vy                                                )\
  i("8xy1",     "OR",  Vx |= Vy                                                )\
  i("8xy2",    "AND",  Vx &= Vy                                                )\
  i("8xy3",    "XOR",  Vx ^= Vy                                                )\
  i("8xy4",    "ADD",  VF = Vx+Vy > 0xFF, Vx += Vy                             )\
  i("8xy5",    "SUB",  VF = Vx >= Vy, Vx -= Vy                                 )\
  i("8xy6",    "SHR",  VF = Vx & 1, Vx >>= 1                                   )\
  i("8xy7",   "NSUB",  VF = Vy >= Vx, Vx = Vy - Vx                             )\
  i("8xyE",    "SHL",  VF = (Vx & 0x80) != 0, Vx <<= 1                         )\
  i("9xy0",  "IFNEQ",  if (Vx != Vy) PC += 2;                                  )\
  i("Annn",   "IREF",  I = NNN                                                 )\
  i("Bnnn",   "JMPO",  PC = V[0] + NNN                                         )\
  i("Cxnn",    "RND",  Vx = rand() & NN                                        )\
  i("Dxyn",   "DRAW",  DrawSpr(Vx, Vy, N)                                      )\
  i("Ex9E",   "KEYP",  if (Vx < 0xF && keys[Vx]) PC += 2;                      )\
  i("ExA1",  "NKEYP",  if (!(Vx < 0xF && keys[Vx])) PC += 2;                   )\
  i("Fx07",   "STDT",  Vx = delay                                              )\
  i("Fx0A",   "KEYW",  WaitKey(&Vx)                                            )\
  i("Fx15",   "LDDT",  delay = Vx                                              )\
  i("Fx18",   "LDST",  sound = Vx                                              )\
  i("Fx1E",   "IADD",  I += Vx, VF = I > 0xFFF, I &= 0xFFF                     )\
  i("Fx29",   "ISPR",  I = (Vx * 5) & 0xFFF                                    )\
  i("Fx33",    "BCD",  PutBCD(Vx)                                              )\
  i("Fx55",  "RDUMP",  for (int i = 0; i <= X; i++) Wr(I+i, V[i]);             )\
  i("Fx65",  "RLOAD",  for (int i = 0; i <= X; i++) V[i] = Rd(I+i);            )

static inline void Instr(UCHAR n0, UCHAR n1, UCHAR n2, UCHAR n3) {
  const UCHAR N = n3;
  const UCHAR NN = (n2 << 4) | N;
  const Uint16 NNN = (n1 << 8) | NN;
  const UCHAR X = n1;
  const UCHAR Y = n2;

#define VF V[0xF]
#define Vx V[X]
#define Vy V[Y]
//#define _MATCHX(p,x) ((p) != '_' ? (((p) >= 'A' ? 0xA+(p)-'A' : (p)-'0') == (x)) : 1)
#define _MATCHX _matchX
#define MATCH(pat, nam, ...)                                                                          \
  else if ((_MATCHX(pat[0], n0) && _MATCHX(pat[1], n1) && _MATCHX(pat[2], n2) && _MATCHX(pat[3], n3)) \
    && ((void)debugprintf("INSTR %.2X: %s [%X%.3X]\n", PC-2, nam, n0,NNN), 1)) { __VA_ARGS__; }

  if (0) {}
  LIST_INSTRS(MATCH)
  else {
    fprintf(stderr, "UNKNOWN OPCODE %X%.3X\n",n0,NNN);
  }

  PC &= 0xFFF;
  SP &= 0xFFF;
}

int beep_bytesremain = 0;
int audiodev = 0;

void RunFrame(void) {
  for (int cycles = 0; cycles < CYCLES_PER_FRAME; cycles++) {
    int thispc = PC;
    PC += 2;
    UCHAR b0 = Rd(thispc),
          b1 = Rd(thispc+1);

    //optimize for first and last nybbles
    const Uint16 n0_n3 = (b0 & 0xF0) | (b1 & 0x0F);

#define O(m) case 0x##m: Instr((0x##m) >> 4, b0 & 0xF, b1 >> 4, (0x##m) & 0xF); break;
#define Q(m) O(m)O(m+1)O(m+2)O(m+3)
#define W(m) Q(m)Q(m+4)Q(m+8)Q(m+12)
    switch (n0_n3) {
      W(00)W(10)W(20)W(30)W(40)
      W(50)W(60)W(70)W(80)W(90)
      W(A0)W(B0)W(C0)W(D0)W(E0)W(F0)
    }
  }
    
  if (delay > 0) delay--;
  if (sound > 0) {
    if (--sound == 0) {
      //play beep sound
      if (audiodev) {
        static Uint16 sine_wave[1000];
        static int sw_samples = sizeof sine_wave / sizeof *sine_wave;
        static Uint8 sine_wave_init = 0;
        if (!sine_wave_init) {
          for (int i = 0; i < sw_samples; i++) {
            extern double sin(double);
            double fade = (sw_samples - i + (sw_samples - 300)) / (double)sw_samples;
            sine_wave[i] = sin(i/2.0) * (0x1p14) * (fade > 1.0 ? 1.0 : fade);
          }
          sine_wave_init = 1;
        }
        SDL_QueueAudio(audiodev, sine_wave, sizeof sine_wave);
      } else printf(" beep\n");
    }
  }
}

int keymap[] = {SDL_SCANCODE_X, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
                SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_A,
                SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Z, SDL_SCANCODE_C,
                SDL_SCANCODE_4, SDL_SCANCODE_R, SDL_SCANCODE_F, SDL_SCANCODE_V};

int main(int argc, char** argv) {
  int res;

  if (argc == 1) return fprintf(stderr, "usage: %s [rom path]\n", *argv), 1;
  
  FILE* rom = fopen(argv[1],"r");
  assert(rom);
  for (short p = 0x200; !feof(rom) && !ferror(rom) && p < sizeof M; p++) {
    M[p] = fgetc(rom);
  }
  fclose(rom);
 
  res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  assert(res == 0);

  SDL_AudioSpec have, want = {
    .freq = 22050, .format = AUDIO_S16SYS,
    .channels = 1, .samples = 4096,
  };
  audiodev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
  if (audiodev) SDL_PauseAudioDevice(audiodev, 0);
  
  SDL_Window* win;
  SDL_Renderer* ren;
  SDL_Texture* tex;
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
  SDL_CreateWindowAndRenderer(W*10,H*10, SDL_WINDOW_RESIZABLE, &win, &ren);
  assert(win && ren);
  SDL_SetWindowTitle(win, "ate8");
  SDL_RenderSetLogicalSize(ren, W, H);

  screen = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, W, H);
  assert(screen && tex);

  SDL_FillRect(screen, NULL, BLACK);

  for (;;) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) goto quit;
      if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        for (int i = 0; i < sizeof keymap / sizeof *keymap; i++) {
          if (keymap[i] == ev.key.keysym.scancode) {
            keys[i] = ev.type == SDL_KEYDOWN;
            break;
          }
        }
      }
    }

    RunFrame();

    SDL_UpdateTexture(tex, NULL, screen->pixels, screen->pitch);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
  }
quit:

  SDL_FreeSurface(screen);
  SDL_DestroyTexture(tex);

  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
}
