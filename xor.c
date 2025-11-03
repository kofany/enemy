#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

const char *info[] = {
    "enemy",
    "0.3",
    "ipv6 edition "
    "[PT] Pojeby Team",
    "We are your worst nightmare. We are the enemy.",
    "Maciek",
    "(fahren) Freudenheim",
    "we based enemy on his",
    "X-men clones",
    NULL,
};

const char *reasons[] = {
    "The pool on the roof must have a leak. [PT] Pojeby Team",
    "We are your worst nightmare. We are the enemy. [PT] Pojeby Team",
    "We are the enemy, you shall fear. [PT] Pojeby Team",
    NULL,
};

const char *realnames[] = {
    "The pool on the roof must have a leak. [PT] Pojeby Team",
    "We are your worst nightmare. We are the enemy. [PT] Pojeby Team",
    "We are the enemy, you shall fear. [PT] Pojeby Team",
    NULL,
};

void xoruj(FILE *, FILE *, const char *, const char **);

int main(int argc, char **argv) {
    FILE *fp, *fp2;

    srand((unsigned int) time(NULL) ^ getpid());

    fp = fopen("enemy.info", "w");
    fp2 = fopen("hide.info", "w");
    if (fp != NULL && fp2 != NULL) {
        xoruj(fp, fp2, "info", info);
    }
    fclose(fp);
    fclose(fp2);

    fp = fopen("enemy.reasons", "w");
    fp2 = fopen("hide.reasons", "w");
    if (fp != NULL && fp2 != NULL) {
        xoruj(fp, fp2, "reasons", reasons);
    }
    fclose(fp);
    fclose(fp2);

    fp = fopen("enemy.realnames", "w");
    fp2 = fopen("hide.realnames", "w");
    if (fp != NULL && fp2 != NULL) {
        xoruj(fp, fp2, "realnames", realnames);
    }
    fclose(fp);
    fclose(fp2);

    return 0;
}

int xrand(float c) {
    return (int)(c * rand() / (RAND_MAX + 1.0));
}

void xoruj(FILE *fp, FILE *fp2, const char *name, const char **arr) {
    int l;
    unsigned char *p, sum, xor, start, diff;
    unsigned char r_start[5] = {0x69, 0x66, 0x59, 0x77, 0x71};
    unsigned char r_diff[5] = {7, 5, 11, 13, 17};

    l = sum = 0;
    fprintf(fp, "// enemy.%s\nstring x%s[] = {\n", name, name);
    fprintf(fp2, "// hide.%s\nchar *h%s[] = {\n", name, name);
    for (; *arr; arr++, l++, sum = 0) {
        start = xor = r_start[xrand(5)];
        diff = r_diff[xrand(5)];
        fprintf(fp, "\t// %s\n", *arr);
        fprintf(fp2, "\t// %s\n\t\"", *arr);
        for (p = (unsigned char *)*arr; *p; p++) {
            sum ^= *p;
            fprintf(fp2, "\\x%x", *p ^ (xor += diff));
        }
        fprintf(fp2, "\",\n");
        fprintf(fp, "\t{0, %d, %d, %d, %d},\n", strlen(*arr), start, sum, diff);
    }
    fprintf(fp2, "};\n");
    fprintf(fp, "};\n");
    fprintf(fp, "const float lx%s = %d.0;\n", name, l);
}
