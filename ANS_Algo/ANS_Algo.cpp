#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CODE_VALUE_BITS 32
#define TOP_VALUE 0xFFFFFFFFu
#define FIRST_QTR (TOP_VALUE/4 + 1)
#define HALF (2 * FIRST_QTR)
#define THIRD_QTR (3 * FIRST_QTR)
#define SYMBOLS 257

// Fonctions d'écriture de bits
void putBit(FILE* out, unsigned char* buffer, int* bitCount, int bit) {
    if (bit)
        *buffer |= (1 << (7 - *bitCount));
    (*bitCount)++;
    if (*bitCount == 8) {
        fputc(*buffer, out);
        *buffer = 0;
        *bitCount = 0;
    }
}

void output_bit(FILE* out, int bit, int* bits_to_follow, unsigned char* buffer, int* bitCount) {
    putBit(out, buffer, bitCount, bit);
    while (*bits_to_follow > 0) {
        putBit(out, buffer, bitCount, !bit);
        (*bits_to_follow)--;
    }
}

// Compression par codage arithmétique
void compressFileArithmetic(const char* inputFileName, const char* outputFileName) {
    FILE* in = fopen(inputFileName, "rb");
    if (!in) {
        fprintf(stderr, "Erreur ouverture %s\n", inputFileName);
        return;
    }
    int freq[SYMBOLS] = { 0 };
    int ch;
    while ((ch = fgetc(in)) != EOF)
        freq[ch]++;
    freq[256] = 1;  // Symbole EOF

    int cum[SYMBOLS + 1];
    cum[0] = 0;
    for (int i = 0; i < SYMBOLS; i++)
        cum[i + 1] = cum[i] + freq[i];
    int total = cum[SYMBOLS];

    FILE* out = fopen(outputFileName, "wb");
    if (!out) {
        fprintf(stderr, "Erreur ouverture %s pour écriture\n", outputFileName);
        fclose(in);
        return;
    }
    fwrite(freq, sizeof(int), SYMBOLS, out);

    fseek(in, 0, SEEK_SET);
    unsigned int low = 0, high = TOP_VALUE;
    int bits_to_follow = 0;
    unsigned char outBuffer = 0;
    int outBitCount = 0;

    while ((ch = fgetc(in)) != EOF) {
        unsigned long long range = (unsigned long long)high - low + 1;
        high = low + (unsigned int)((range * cum[ch + 1]) / total) - 1;
        low = low + (unsigned int)((range * cum[ch]) / total);
        while (1) {
            if (high < HALF) {
                output_bit(out, 0, &bits_to_follow, &outBuffer, &outBitCount);
            }
            else if (low >= HALF) {
                output_bit(out, 1, &bits_to_follow, &outBuffer, &outBitCount);
                low -= HALF;
                high -= HALF;
            }
            else if (low >= FIRST_QTR && high < THIRD_QTR) {
                bits_to_follow++;
                low -= FIRST_QTR;
                high -= FIRST_QTR;
            }
            else {
                break;
            }
            low = low << 1;
            high = (high << 1) | 1;
        }
    }
    { // Encodage du symbole EOF (256)
        unsigned long long range = (unsigned long long)high - low + 1;
        high = low + (unsigned int)((range * cum[256 + 1]) / total) - 1;
        low = low + (unsigned int)((range * cum[256]) / total);
        while (1) {
            if (high < HALF) {
                output_bit(out, 0, &bits_to_follow, &outBuffer, &outBitCount);
            }
            else if (low >= HALF) {
                output_bit(out, 1, &bits_to_follow, &outBuffer, &outBitCount);
                low -= HALF;
                high -= HALF;
            }
            else if (low >= FIRST_QTR && high < THIRD_QTR) {
                bits_to_follow++;
                low -= FIRST_QTR;
                high -= FIRST_QTR;
            }
            else {
                break;
            }
            low = low << 1;
            high = (high << 1) | 1;
        }
        bits_to_follow++;
        if (low < FIRST_QTR)
            output_bit(out, 0, &bits_to_follow, &outBuffer, &outBitCount);
        else
            output_bit(out, 1, &bits_to_follow, &outBuffer, &outBitCount);
    }
    if (outBitCount > 0)
        fputc(outBuffer, out);
    fclose(in);
    fclose(out);
}

// Lecture d'un bit pour la décompression arithmétique
int readBitArithmetic(FILE* in, unsigned char* buffer, int* bitCount) {
    if (*bitCount == 0) {
        int ch = fgetc(in);
        if (ch == EOF) return 0;
        *buffer = (unsigned char)ch;
        *bitCount = 8;
    }
    int bit = (*buffer >> 7) & 1;
    *buffer <<= 1;
    (*bitCount)--;
    return bit;
}

// Décompression par codage arithmétique (version corrigée)
// Remarque : la mise à jour des bornes est réalisée de façon symétrique à l'encodage.
void decompressFileArithmetic(const char* inputFileName, const char* outputFileName) {
    FILE* in = fopen(inputFileName, "rb");
    if (!in) {
        fprintf(stderr, "Erreur ouverture %s\n", inputFileName);
        return;
    }
    int freq[SYMBOLS] = { 0 };
    if (fread(freq, sizeof(int), SYMBOLS, in) != SYMBOLS) {
        fprintf(stderr, "Erreur lecture en-tête dans %s\n", inputFileName);
        fclose(in);
        return;
    }
    int cum[SYMBOLS + 1];
    cum[0] = 0;
    for (int i = 0; i < SYMBOLS; i++)
        cum[i + 1] = cum[i] + freq[i];
    int total = cum[SYMBOLS];

    FILE* out = fopen(outputFileName, "wb");
    if (!out) {
        fprintf(stderr, "Erreur ouverture %s pour écriture\n", outputFileName);
        fclose(in);
        return;
    }

    unsigned int low = 0, high = TOP_VALUE, value = 0;
    unsigned char inBuffer = 0;
    int inBitCount = 0;
    for (int i = 0; i < 4; i++) {
        int ch = fgetc(in);
        value = (value << 8) | (ch & 0xFF);
    }

    while (1) {
        unsigned long long range = (unsigned long long)high - low + 1;
        unsigned long long scaled_value = (((unsigned long long)(value - low + 1)) * total - 1) / range;
        int symbol;
        for (symbol = 0; symbol < SYMBOLS; symbol++) {
            if (cum[symbol + 1] > scaled_value)
                break;
        }
        if (symbol == 256)  // symbole EOF détecté
            break;
        fputc(symbol, out);

        // Mise à jour des bornes en décompression : IMPORTANT !  
        unsigned int new_low = low + (unsigned int)((range * cum[symbol]) / total);
        unsigned int new_high = low + (unsigned int)((range * cum[symbol + 1]) / total) - 1;
        low = new_low;
        high = new_high;

        while (1) {
            if (high < HALF) {
                // rien
            }
            else if (low >= HALF) {
                low -= HALF;
                high -= HALF;
                value -= HALF;
            }
            else if (low >= FIRST_QTR && high < THIRD_QTR) {
                low -= FIRST_QTR;
                high -= FIRST_QTR;
                value -= FIRST_QTR;
            }
            else {
                break;
            }
            low = low << 1;
            high = (high << 1) | 1;
            value = (value << 1) | readBitArithmetic(in, &inBuffer, &inBitCount);
        }
    }
    fclose(in);
    fclose(out);
}

// Vérification : compare deux fichiers octet par octet
int verifyFiles(const char* file1, const char* file2) {
    FILE* f1 = fopen(file1, "rb");
    FILE* f2 = fopen(file2, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return 0;
    }
    int result = 1, ch1, ch2;
    do {
        ch1 = fgetc(f1);
        ch2 = fgetc(f2);
        if (ch1 != ch2) { result = 0; break; }
    } while (ch1 != EOF && ch2 != EOF);
    fclose(f1);
    fclose(f2);
    return result;
}

int main() {
    int i = 1;
    char inputFileName[100], compFileName[100], decompFileName[100];

    while (1) {
        sprintf(inputFileName, "C:/Users/donde/source/test/random/%dbook.txt", i);
        FILE* testFile = fopen(inputFileName, "rb");
        if (!testFile)
            break;
        fclose(testFile);

        sprintf(compFileName, "C:/Users/donde/source/test/random/%dbook.arith", i);
        sprintf(decompFileName, "C:/Users/donde/source/test/random/%dbook_dearith.txt", i);

        clock_t start = clock();
        compressFileArithmetic(inputFileName, compFileName);
        clock_t end = clock();
        printf("Compression Arithmetic: %s -> %s en %.3f s.\n",
            inputFileName, compFileName, (double)(end - start) / CLOCKS_PER_SEC);

        start = clock();
        decompressFileArithmetic(compFileName, decompFileName);
        end = clock();
        printf("Décompression Arithmetic: %s -> %s en %.3f s.\n",
            compFileName, decompFileName, (double)(end - start) / CLOCKS_PER_SEC);

        if (verifyFiles(inputFileName, decompFileName))
            printf("Vérification : %s et %s sont identiques.\n\n", inputFileName, decompFileName);
        else
            printf("Vérification : %s et %s diffèrent.\n\n", inputFileName, decompFileName);
        if (i++ >= 1)
            break;
    }
    if (i == 0)
        printf("Aucun fichier à compresser/décompresser.\n");
    return 0;
}
