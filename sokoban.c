/*
 * This code was based on Sokoban C available at
 * http://www.rosettacode.org/wiki/Sokoban#C
 * licenced under GNU Free Documentation License 1.2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <omp.h>
#include <time.h>

int w, h, n_boxes;
uint8_t * board, * goals, * live;

typedef uint16_t cidx_t;
typedef uint32_t hash_t;

/* board configuration is represented by an array of cell indices
   of player and boxes */
typedef struct state_t state_t;
void show_moves(const state_t * s, int nextPos);
struct state_t { // variable length
    hash_t h;
    state_t * prev, * next, * qnext;
    cidx_t c[];
};

size_t state_size, block_size = 32;
state_t * block_root, * block_head;


inline
state_t * newstate(state_t * parent) {
    inline state_t * next_of(state_t * s) {
        return (void * )((uint8_t * ) s + state_size);
    }

    state_t * ptr;
    if (!block_head) {
        block_size *= 2;
        state_t * p = malloc(block_size * state_size);
        assert(p);
        p -> next = block_root;
        block_root = p;
        ptr = (void * )((uint8_t * ) p + state_size * block_size);
        p = block_head = next_of(p);
        state_t * q;
        for (q = next_of(p); q < ptr; p = q, q = next_of(q))
            p -> next = q;
        p -> next = NULL;
    }

    ptr = block_head;
    block_head = block_head -> next;

    ptr -> prev = parent;
    ptr -> h = 0;
    return ptr;
}

inline
void unnewstate(state_t * p) {
    p -> next = block_head;
    block_head = p;
}

enum {
    space,
    wall,
    player,
    box
};

// mark up positions where a box definitely should not be
void mark_live(const int c) {
    const int y = c / w,
        x = c % w;
    if (live[c]) return;

    live[c] = 1;
    if (y > 1 && board[c - w] != wall && board[c - w * 2] != wall)
        mark_live(c - w);
    if (y < h - 2 && board[c + w] != wall && board[c + w * 2] != wall)
        mark_live(c + w);
    if (x > 1 && board[c - 1] != wall && board[c - 2] != wall)
        mark_live(c - 1);
    if (x < w - 2 && board[c + 1] != wall && board[c + 2] != wall)
        mark_live(c + 1);
}

state_t * parse_board(const char * s) {
    board = calloc(w * h, sizeof(uint8_t));
    assert(board);
    goals = calloc(w * h, sizeof(uint8_t));
    assert(goals);
    live = calloc(w * h, sizeof(uint8_t));
    assert(live);

    n_boxes = 0;
    for (int i = 0; s[i]; i++) {
        switch (s[i]) {
        case '#':
            board[i] = wall;
            continue;

        case '.': // fallthrough
        case '+':
            goals[i] = 1; // fallthrough
        case '@':
            continue;

        case '*':
            goals[i] = 1; // fallthrough
        case '$':
            n_boxes++;
            continue;
        default:
            continue;
        }
    }

    const int is = sizeof(int);
    state_size = (sizeof(state_t) + (1 + n_boxes) * sizeof(cidx_t) + is - 1) /
        is * is;

    state_t * state = newstate(NULL);

    for (int i = 0, j = 0; i < w * h; i++) {
        if (goals[i]) mark_live(i);
        if (s[i] == '$' || s[i] == '*')
            state -> c[++j] = i;
        else if (s[i] == '@' || s[i] == '+')
            state -> c[0] = i;
    }

    return state;
}

// K&R hash function
inline
void hash(state_t * s) {
    if (!s -> h) {
        register hash_t ha = 0;
        cidx_t * p = s -> c;
        for (int i = 0; i <= n_boxes; i++)
            ha = p[i] + 31 * ha;
        s -> h = ha;
    }
}

state_t ** buckets;
hash_t hash_size, fill_limit, filled;

void extend_table() {
    int old_size = hash_size;

    if (!old_size) {
        hash_size = 1024;
        filled = 0;
        fill_limit = hash_size * 3 / 4; // 0.75 load factor
    } else {
        hash_size *= 2;
        fill_limit *= 2;
    }

    buckets = realloc(buckets, sizeof(state_t * ) * hash_size);
    assert(buckets);

    // rehash
    memset(buckets + old_size, 0, sizeof(state_t * ) * (hash_size - old_size));

    const hash_t bits = hash_size - 1;
    for (int i = 0; i < old_size; i++) {
        state_t * head = buckets[i];
        buckets[i] = NULL;
        while (head) {
            state_t * next = head -> next;
            const int j = head -> h & bits;
            head -> next = buckets[j];
            buckets[j] = head;
            head = next;
        }
    }
}

state_t * lookup(state_t * s) {
    hash(s);    
    state_t * f = buckets[s -> h & (hash_size - 1)];
    for (; f; f = f -> next) {
        if ( //(f->h == s->h) &&
            !memcmp(s -> c, f -> c, sizeof(cidx_t) * (1 + n_boxes)))
            break;
    }

    return f;
}

bool add_to_table(state_t * s) {
    #pragma omp critical
    {
    if (lookup(s)) {
        unnewstate(s);
        return false;
    }    
        
        if (filled++ >= fill_limit)
            extend_table();

        hash_t i = s -> h & (hash_size - 1);

        s -> next = buckets[i];
        buckets[i] = s;
    }
    return true;
}

bool success(const state_t * s) {
    for (int i = 1; i <= n_boxes; i++)
        if (!goals[s -> c[i]]) return false;
    return true;
}

state_t * move_me(state_t * s,
    const int dy,
        const int dx) {
    const int y = s -> c[0] / w;
    const int x = s -> c[0] % w;
    const int y1 = y + dy;
    const int x1 = x + dx;
    const int c1 = y1 * w + x1;

    if (y1 < 0 || y1 > h || x1 < 0 || x1 > w ||
        board[c1] == wall)
        {
            return NULL;
        }
        

    int at_box = 0;
    for (int i = 1; i <= n_boxes; i++) {
        if (s -> c[i] == c1) {
            at_box = i;
            break;
        }
    }

    int c2;
    if (at_box) {
        c2 = c1 + dy * w + dx;
        if (board[c2] == wall || !live[c2])
            return NULL;
        for (int i = 1; i <= n_boxes; i++)
            if (s -> c[i] == c2) return NULL;
    }

    state_t * n = newstate(s);
    memcpy(n -> c + 1, s -> c + 1, sizeof(cidx_t) * n_boxes);

    cidx_t * p = n -> c;
    p[0] = c1;

    if (at_box) p[at_box] = c2;

    // leet bubble sort
    for (int i = n_boxes; --i;) {
        cidx_t t = 0;
        for (int j = 1; j < i; j++) {
            if (p[j] > p[j + 1])
                t = p[j], p[j] = p[j + 1], p[j + 1] = t;
        }
        if (!t) break;
    }
    return n;
}

state_t * next_level, * done;

bool queue_move(state_t * s) {    
        if (!s || !add_to_table(s))
        return false;

        if (success(s)) {
            done = s;
            return true;
        }
    #pragma omp critical
    {    
        s -> qnext = next_level;
        next_level = s;
        
    }
    return false;
}

bool do_move(state_t * s) {
    state_t* news[4];
    #pragma omp task shared(s,news) 
        news[0]=move_me(s, 0, 1);
    #pragma omp task shared(s,news) 
        news[1]=move_me(s, 0, -1);
    #pragma omp task shared(s,news) 
        news[2]=move_me(s, -1, 0);
    #pragma omp task shared(s,news) 
        news[3]=move_me(s, 1, 0);
    
    #pragma omp taskwait

    return queue_move(news[0]) || queue_move(news[1]) || queue_move(news[2]) || queue_move(news[3]) ;
}

void show_moves(const state_t * s, int nextPos) {
    if (s -> prev)
        show_moves(s -> prev, s -> c[0]);
    if (nextPos == -1) {
        printf("\n");
        return;
    }
    int cx = s -> c[0] % w;
    int cy = s -> c[0] / w;
    int nx = nextPos % w;
    int ny = nextPos / w;
    int box = 0;
    for (int i = 1; !box && i <= n_boxes; i++)
        box = s -> c[i] == nextPos;
    if (cx < nx) printf(box ? "R" : "r");
    else if (cx > nx) printf(box ? "L" : "l");
    else if (cy < ny) printf(box ? "D" : "d");
    else if (cy > ny) printf(box ? "U" : "u");
    else if (1) {
        printf("Invalid move\n");
        exit(1);
    }
}

int main() {
    clock_t inicial_time;
    clock_t final_time;
    double time_passed;
    inicial_time = clock();
    size_t sz = 1024;
    char * boardStr = malloc(sz * sizeof(char));
    char * pos = boardStr;
    w = -1;
    h = 0;
    while (1) {
        int read = getline( & pos, & sz, stdin);
        if (read == -1) break;
        if (read >= w) w = read;
        sz -= read;
        pos += read;
        h++;
    }
    state_t * s = parse_board(boardStr);
    free(boardStr);

    extend_table();
    queue_move(s);
    while (!done) {
        state_t * head = next_level;
        for (next_level = NULL; head && !done; head = head -> qnext)
        {
            #pragma omp task shared(head)
                do_move(head);
        }
        #pragma omp taskwait

        if (!next_level) {
            puts("no solution?");
            return 1;
        }
    }

    show_moves(done, -1);

    free(buckets);
    free(board);
    free(goals);
    free(live);

    while (block_root) {
        void * tmp = block_root -> next;
        free(block_root);
        block_root = tmp;
    }
    final_time = clock();
    time_passed = ((double)(final_time - inicial_time) )/CLOCKS_PER_SEC;
    printf("\ntime passed = %f", time_passed);
    return 0;
}
