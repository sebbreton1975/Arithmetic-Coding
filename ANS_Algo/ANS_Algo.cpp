#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//
// Paramètres et macros pour le codage arithmétique
//
#define CODE_VALUE_BITS 32
#define TOP_VALUE 0xFFFFFFFFu
#define FIRST_QTR (TOP_VALUE/4 + 1)  // 0x40000000
#define HALF      (2 * FIRST_QTR)    // 0x80000000
#define THIRD_QTR (3 * FIRST_QTR)    // 0xC0000000

// Nombre de symboles : 256 pour les octets + 1 symbole EOF
#define SYMBOLS 257

//
// Fonction putBit : écrit un bit dans le flux de sortie via un tampon
//
void putBit(FILE* out, int bit, unsigned char* outBuffer, int* outBitCount) {
    if (bit)
        *outBuffer |= (1 << (7 - *outBitCount));
    (*outBitCount)++;
    if (*outBitCount == 8) {
        fputc(*outBuffer, out);
        *outBuffer = 0;
        *outBitCount = 0;
    }
}

//
// Fonction output_bit : écrit un bit et, pour chaque bit différé (bits_to_follow),
// écrit le complément (bit inversé)
//
void output_bit(FILE* out, int bit, int* bits_to_follow, unsigned char* outBuffer, int* outBitCount) {
    putBit(out, bit, outBuffer, outBitCount);
    while (*bits_to_follow > 0) {
        putBit(out, !bit, outBuffer, outBitCount);
        (*bits_to_follow)--;
    }
}

//
// Fonction compressFileArithmetic : réalise la compression par codage arithmétique
//   - Lit le fichier d'entrée et construit une table de fréquences pour 0..255, en ajoutant
//     une occurrence pour le symbole EOF (indice 256)
//   - Construit la table cumulative
//   - Encode les symboles (puis le symbole EOF) en mettant à jour les bornes low et high
//     avec gestion du décalage (scaling) et du sous-débordement.
//   - Écrit la table de fréquences en entête du fichier de sortie
//
void compressFileArithmetic(const char* inputFileName, const char* outputFileName) {
    FILE* in = fopen(inputFileName, "rb");
    if (!in) {
        fprintf(stderr, "Erreur d'ouverture du fichier %s\n", inputFileName);
        return;
    }

    // Calcul des fréquences pour les 256 octets et pour le symbole EOF (indice 256)
    int freq[SYMBOLS] = { 0 };
    int ch;
    while ((ch = fgetc(in)) != EOF) {
        freq[ch]++;
    }
    // Assurer que le symbole EOF apparaisse (pour la décompression)
    freq[256] = 1;

    // Construction de la table cumulative : cum[0] = 0 et pour i de 0 à SYMBOLS-1
    int cum[SYMBOLS + 1];
    cum[0] = 0;
    for (int i = 0; i < SYMBOLS; i++) {
        cum[i + 1] = cum[i] + freq[i];
    }
    int total = cum[SYMBOLS];  // somme totale des fréquences

    // Ouverture du fichier de sortie en mode binaire et écriture de la table de fréquences
    FILE* out = fopen(outputFileName, "wb");
    if (!out) {
        fprintf(stderr, "Erreur d'ouverture du fichier %s pour l'écriture\n", outputFileName);
        fclose(in);
        return;
    }
    fwrite(freq, sizeof(int), SYMBOLS, out);

    // Remise à zéro du pointeur d'entrée
    fseek(in, 0, SEEK_SET);

    // Initialisation des bornes pour le codage arithmétique
    unsigned int low = 0;
    unsigned int high = TOP_VALUE;
    int bits_to_follow = 0;

    // Variables pour la gestion de la sortie bit à bit
    unsigned char outBuffer = 0;
    int outBitCount = 0;

    // Encodage de chaque symbole du fichier d'entrée
    while ((ch = fgetc(in)) != EOF) {
        // Calcul de la plage actuelle
        unsigned long range = (unsigned long)high - low + 1;
        // Mise à jour des bornes en fonction des fréquences cumulées
        high = low + (unsigned int)((range * cum[ch + 1]) / total) - 1;
        low = low + (unsigned int)((range * cum[ch]) / total);

        // Boucle de mise à l'échelle (scaling)
        while (1) {
            if (high < HALF) {
                // La borne haute est dans la première moitié : émettre un 0
                output_bit(out, 0, &bits_to_follow, &outBuffer, &outBitCount);
            }
            else if (low >= HALF) {
                // La borne basse est dans la seconde moitié : émettre un 1
                output_bit(out, 1, &bits_to_follow, &outBuffer, &outBitCount);
                low -= HALF;
                high -= HALF;
            }
            else if (low >= FIRST_QTR && high < THIRD_QTR) {
                // Cas de sous-débordement : on décale et on compte le bit différé
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

    // Encodage du symbole EOF (indice 256)
    {
        unsigned long range = (unsigned long)high - low + 1;
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
        // Terminer en émettant un bit complémentaire
        bits_to_follow++;
        if (low < FIRST_QTR)
            output_bit(out, 0, &bits_to_follow, &outBuffer, &outBitCount);
        else
            output_bit(out, 1, &bits_to_follow, &outBuffer, &outBitCount);
    }

    // Écriture des derniers bits restant dans le tampon
    if (outBitCount > 0) {
        fputc(outBuffer, out);
    }

    fclose(in);
    fclose(out);
}

//
// Fonction main : boucle sur les fichiers "0.txt", "1.txt", etc.
// Mesure le temps de compression pour chacun et affiche un message récapitulatif.
//
int main() {
    int i = 1;
    char inputFileName[100];
    char outputFileName[100];

    while (1) {
        sprintf(inputFileName, "C:/Users/donde/source/test/random/%dbook.txt", i);
        FILE* testFile = fopen(inputFileName, "rb");
        if (!testFile) {
            break; // Arrête la boucle si le fichier n'existe pas
        }
        fclose(testFile);

        sprintf(outputFileName, "C:/Users/donde/source/test/random/%dbook.arith", i);

        clock_t start = clock();
        compressFileArithmetic(inputFileName, outputFileName);
        clock_t end = clock();
        double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
        printf("Compression du fichier %s en %s terminee en %.3f secondes.\n",
            inputFileName, outputFileName, time_spent);
        if (i++ >= 1)
            break;
    }

    if (i == 0)
        printf("Aucun fichier a compresser.\n");

    return 0;
}
