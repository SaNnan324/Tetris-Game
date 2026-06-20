#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <chrono>

// Terminal control
struct termios orig_termios;

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// ANSI escape codes
#define CLEAR "\033[2J\033[H"
#define HIDE_CURSOR "\033[?25l"
#define SHOW_CURSOR "\033[?25h"
#define RESET "\033[0m"

// Colors
const std::string COLORS[] = {
    "\033[0m",       // 0 = empty
    "\033[41m",      // 1 = I - red
    "\033[42m",      // 2 = O - green
    "\033[43m",      // 3 = T - yellow
    "\033[44m",      // 4 = S - blue
    "\033[45m",      // 5 = Z - magenta
    "\033[46m",      // 6 = J - cyan
    "\033[47m",      // 7 = L - white
};

const int BOARD_W = 10;
const int BOARD_H = 20;

// Tetromino shapes [piece][rotation][row][col]
// Each piece has 4 rotations, each rotation is 4x4
const int PIECES[7][4][4][4] = {
    // I
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    // O
    {{{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}}},
    // T
    {{{0,0,0,0},{0,1,0,0},{1,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,1,0,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,0},{0,1,0,0}},
     {{0,0,0,0},{0,1,0,0},{1,1,0,0},{0,1,0,0}}},
    // S
    {{{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,0,1,0}}},
    // Z
    {{{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,0,1,0},{0,1,1,0},{0,1,0,0}},
     {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,0,1,0},{0,1,1,0},{0,1,0,0}}},
    // J
    {{{0,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{0,1,0,0},{0,1,0,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,1,0,0},{0,1,0,0},{1,1,0,0}}},
    // L
    {{{0,0,0,0},{0,0,1,0},{1,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,0,0},{0,1,0,0},{0,1,1,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,0},{1,0,0,0}},
     {{0,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,0,0}}},
};

int board[BOARD_H][BOARD_W] = {};
int score = 0;
int level = 1;
int lines = 0;
bool gameOver = false;

// Current piece
int curPiece = 0;
int curRot = 0;
int curX = 0, curY = 0;
int nextPiece = 0;

bool isValidPos(int piece, int rot, int x, int y) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (PIECES[piece][rot][r][c]) {
                int nx = x + c;
                int ny = y + r;
                if (nx < 0 || nx >= BOARD_W || ny >= BOARD_H) return false;
                if (ny >= 0 && board[ny][nx]) return false;
            }
        }
    }
    return true;
}

void lockPiece() {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (PIECES[curPiece][curRot][r][c]) {
                int ny = curY + r;
                if (ny < 0) { gameOver = true; return; }
                board[ny][curX + c] = curPiece + 1;
            }
}

int clearLines() {
    int cleared = 0;
    for (int r = BOARD_H - 1; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < BOARD_W; c++)
            if (!board[r][c]) { full = false; break; }
        if (full) {
            for (int rr = r; rr > 0; rr--)
                memcpy(board[rr], board[rr-1], sizeof(board[0]));
            memset(board[0], 0, sizeof(board[0]));
            cleared++;
            r++;
        }
    }
    return cleared;
}

void spawnPiece() {
    curPiece = nextPiece;
    nextPiece = rand() % 7;
    curRot = 0;
    curX = BOARD_W / 2 - 2;
    curY = -1;
    if (!isValidPos(curPiece, curRot, curX, curY))
        gameOver = true;
}

void drawCell(int color, bool empty = false) {
    if (empty || color == 0)
        std::cout << RESET << "  ";
    else
        std::cout << COLORS[color] << "  " << RESET;
}

void render() {
    // Build display board
    int display[BOARD_H][BOARD_W];
    memcpy(display, board, sizeof(board));

    // Ghost piece
    int ghostY = curY;
    while (isValidPos(curPiece, curRot, curX, ghostY + 1)) ghostY++;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (PIECES[curPiece][curRot][r][c]) {
                int ny = ghostY + r, nx = curX + c;
                if (ny >= 0 && ny < BOARD_H && nx >= 0 && nx < BOARD_W && !display[ny][nx])
                    display[ny][nx] = -1; // ghost marker
            }

    // Draw current piece
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (PIECES[curPiece][curRot][r][c]) {
                int ny = curY + r, nx = curX + c;
                if (ny >= 0 && ny < BOARD_H)
                    display[ny][nx] = curPiece + 1;
            }

    std::string out = CLEAR;
    out += "\033[1m  === TETRIS ===\033[0m\n\n";

    for (int r = 0; r < BOARD_H; r++) {
        out += "\033[37m|\033[0m";
        for (int c = 0; c < BOARD_W; c++) {
            int v = display[r][c];
            if (v == -1)
                out += "\033[37m[]\033[0m"; // ghost
            else if (v == 0)
                out += "  ";
            else
                out += COLORS[v] + "  " + RESET;
        }
        out += "\033[37m|\033[0m";

        // Side panel
        if (r == 0)  out += "  \033[1mNEXT:\033[0m";
        if (r >= 1 && r <= 4) {
            int nr = r - 1;
            out += "  ";
            for (int c = 0; c < 4; c++) {
                int v = PIECES[nextPiece][0][nr][c];
                if (v)
                    out += COLORS[nextPiece + 1] + "  " + RESET;
                else
                    out += "  ";
            }
        }
        if (r == 6)  out += "  \033[1mSCORE:\033[0m " + std::to_string(score);
        if (r == 7)  out += "  \033[1mLEVEL:\033[0m " + std::to_string(level);
        if (r == 8)  out += "  \033[1mLINES:\033[0m " + std::to_string(lines);
        if (r == 10) out += "  \033[90m← → Move\033[0m";
        if (r == 11) out += "  \033[90m↑  Rotate\033[0m";
        if (r == 12) out += "  \033[90m↓  Soft drop\033[0m";
        if (r == 13) out += "  \033[90mSpc Hard drop\033[0m";
        if (r == 14) out += "  \033[90m q  Quit\033[0m";

        out += "\n";
    }
    out += "\033[37m+" + std::string(BOARD_W * 2, '-') + "+\033[0m\n";

    std::cout << out;
    std::cout.flush();
}

int readKey() {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return 0;
    if (c == '\033') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) <= 0) return '\033';
        if (read(STDIN_FILENO, &seq[1], 1) <= 0) return '\033';
        if (seq[0] == '[') {
            if (seq[1] == 'A') return 'w'; // up
            if (seq[1] == 'B') return 's'; // down
            if (seq[1] == 'C') return 'd'; // right
            if (seq[1] == 'D') return 'a'; // left
        }
        return '\033';
    }
    return c;
}

int main() {
    srand(time(nullptr));
    enableRawMode();
    std::cout << HIDE_CURSOR;

    nextPiece = rand() % 7;
    spawnPiece();

    auto getDelay = [&]() { return std::max(100000, 500000 - (level - 1) * 40000); };

    struct timeval tv;
    auto lastDrop = std::chrono::steady_clock::now();

    // Use simple timer loop
    while (!gameOver) {
        // Non-blocking input
        int key = readKey();
        if (key == 'q' || key == 'Q') break;
        if (key == 'a' && isValidPos(curPiece, curRot, curX - 1, curY)) curX--;
        if (key == 'd' && isValidPos(curPiece, curRot, curX + 1, curY)) curX++;
        if (key == 'w') {
            int newRot = (curRot + 1) % 4;
            if (isValidPos(curPiece, newRot, curX, curY)) curRot = newRot;
            else if (isValidPos(curPiece, newRot, curX - 1, curY)) { curRot = newRot; curX--; }
            else if (isValidPos(curPiece, newRot, curX + 1, curY)) { curRot = newRot; curX++; }
        }
        if (key == 's') {
            if (isValidPos(curPiece, curRot, curX, curY + 1)) curY++;
            else {
                lockPiece();
                int c = clearLines();
                if (c) {
                    const int pts[] = {0, 100, 300, 500, 800};
                    score += pts[c] * level;
                    lines += c;
                    level = lines / 10 + 1;
                }
                spawnPiece();
            }
        }
        if (key == ' ') {
            while (isValidPos(curPiece, curRot, curX, curY + 1)) curY++;
            score += 2;
            lockPiece();
            int c = clearLines();
            if (c) {
                const int pts[] = {0, 100, 300, 500, 800};
                score += pts[c] * level;
                lines += c;
                level = lines / 10 + 1;
            }
            spawnPiece();
        }

        // Auto drop
        using namespace std::chrono;
        auto now = steady_clock::now();
        auto elapsed = duration_cast<microseconds>(now - lastDrop).count();
        if (elapsed >= getDelay()) {
            if (isValidPos(curPiece, curRot, curX, curY + 1)) {
                curY++;
            } else {
                lockPiece();
                int c = clearLines();
                if (c) {
                    const int pts[] = {0, 100, 300, 500, 800};
                    score += pts[c] * level;
                    lines += c;
                    level = lines / 10 + 1;
                }
                spawnPiece();
            }
            lastDrop = now;
        }

        render();
        usleep(16000); // ~60fps input polling
    }

    disableRawMode();
    std::cout << SHOW_CURSOR << CLEAR;
    if (gameOver)
        std::cout << "\033[1;31m  GAME OVER!\033[0m\n";
    std::cout << "  Final Score: " << score << "\n";
    std::cout << "  Lines: " << lines << "  Level: " << level << "\n\n";
    return 0;
}
