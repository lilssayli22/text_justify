#define _GNU_SOURCE  // Nécessaire pour activer certaines extensions POSIX (ex : mmap)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>   // mmap(), munmap()
#include <sys/stat.h>   // fstat()
#include <fcntl.h>      // open(), O_RDONLY
#include <unistd.h>     // close()
#include <ctype.h>      // isspace(), isprint()
#include <errno.h>
#include <limits.h>     // LLONG_MAX

// =====================================================================
// Constantes globales — bornes et tailles maximales
// =====================================================================
#define MAX_WORDS 1000000     // Limite maximale de mots par paragraphe
#define MAX_WORD_LEN 256      // Longueur maximale d'un mot
#define MAX_LINE_LEN 10000    // Largeur maximale autorisée pour une ligne

typedef long long ll;

// =====================================================================
// Structures principales : mot et paragraphe
// =====================================================================
typedef struct {
    char *start;  // Pointeur vers le début du mot dans le fichier mappé
    int len;      // Longueur du mot (en caractères)
} Word;

typedef struct {
    Word *words;      // Tableau dynamique de mots
    int word_count;   // Nombre total de mots dans le paragraphe
    ll optimal_cost;  // Coût optimal de justification (calculé par DP)
} Paragraph;

// =====================================================================
// Gestion des erreurs : affichage normalisé et sortie propre
// =====================================================================
void error(const char *msg) {
    fprintf(stderr, "AODjustify ERROR> %s\n", msg);
    exit(EXIT_FAILURE);
}

// =====================================================================
// Vérification du format d'encodage ISO-8859-1
// =====================================================================
void validate_iso8859_1(const char *filename) {
    char cmd[512];
    // Vérifie que le fichier est bien en ISO-8859-1, ISO-8859-15 ou US-ASCII
    snprintf(cmd, sizeof(cmd),
             "file -i '%s' 2>/dev/null | grep -Eqi 'iso-8859-(1|15)|us-ascii'",
             filename);
    int ret = system(cmd);
    if (ret != 0) {
        error("fichier en entree pas au format ISO-8859-1");
    }
}

// =====================================================================
// Lecture du fichier avec mmap (accès mémoire direct, sans buffer)
// =====================================================================
char *map_file(const char *filename, size_t *size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) error("Ouverture fichier impossible");

    struct stat sb;
    if (fstat(fd, &sb) == -1) error("Impossible de lire la taille du fichier");

    *size = sb.st_size;
    if (*size == 0) error("Fichier vide");

    // mmap : mappe le fichier entier en mémoire pour lecture rapide
    char *data = mmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) error("Erreur mmap");

    close(fd);
    return data;
}

// =====================================================================
// Fonctions utilitaires — détection des séparateurs et caractères valides
// =====================================================================
int is_separator(char c) {
    // Espace, tabulation ou retour à la ligne
    return (c == ' ' || c == '\n' || c == '\t' || c == '\r');
}

int is_printable(char c) {
    // Caractères visibles selon ISO-8859-1
    unsigned char uc = (unsigned char)c;
    return (uc >= 33 && uc <= 126) || (uc >= 0xA0);
}

// =====================================================================
// Découpage du fichier mappé en paragraphes et mots
// =====================================================================
int extract_paragraphs(char *data, size_t size, Paragraph *paragraphs, int *para_count) {
    *para_count = 0;
    size_t i = 0;

    while (i < size) {
        // Ignorer les espaces, tabulations et retours à la ligne
        while (i < size && is_separator(data[i])) i++;
        if (i >= size) break;

        // Création d'un nouveau paragraphe
        Paragraph *p = &paragraphs[*para_count];
        p->words = malloc(MAX_WORDS * sizeof(Word));
        if (!p->words) error("Erreur d'allocation memoire");
        p->word_count = 0;

        // Lecture du paragraphe jusqu'à un double retour à la ligne
        while (i < size) {
            if (i + 1 < size && data[i] == '\n' && data[i + 1] == '\n') {
                i += 2;
                break;
            }
            
            // Ignorer les séparateurs simples
            if (is_separator(data[i])) {
                i++;
                continue;
            }

            // Extraction d'un mot complet
            if (is_printable(data[i])) {
                Word *w = &p->words[p->word_count];
                w->start = &data[i];
                w->len = 0;

                // Comptage des caractères du mot
                while (i < size && is_printable(data[i])) {
                    w->len++;
                    i++;
                    if (w->len > MAX_WORD_LEN)
                        error("mot de longueur superieure a 256 caracteres");
                }

                if (++p->word_count >= MAX_WORDS)
                    error("trop de mots dans un paragraphe");
            } else {
                i++;
            }
        }

        // Si le paragraphe contient au moins un mot, on le conserve
        if (p->word_count > 0) {
            (*para_count)++;
        } else {
            free(p->words);
        }

        if (*para_count >= 1000)
            error("trop de paragraphes");
    }

    return *para_count;
}

// =====================================================================
// Fonction cube(x) protégée contre les débordements d'entiers
// =====================================================================
ll cube(ll x) { 
    if (x > 2097151 || x < -2097151) return LLONG_MAX;
    return x * x * x; 
}

// =====================================================================
// Calcul de la longueur Δ(i,k) d'une ligne (sommes cumulées)
// =====================================================================
ll delta(int i, int k, ll *prefix_sum) {
    return prefix_sum[k] - (i > 0 ? prefix_sum[i - 1] : 0) + (k - i);
}

// =====================================================================
// Programmation dynamique : justification optimale d'un paragraphe
// =====================================================================
ll justify_paragraph(Paragraph *p, ll M, int *next_break) {
    int n = p->word_count;
    if (n == 0) return 0;
    
    // Tables de DP (valeurs de coût et validité)
    ll *dp = malloc(sizeof(ll) * (n + 1));
    int *valid = malloc(sizeof(int) * (n + 1));
    if (!dp || !valid) error("Erreur d'allocation memoire");

    for (int i = 0; i <= n; i++) {
        dp[i] = LLONG_MAX;
        valid[i] = 0;
        next_break[i] = i;
    }
    dp[n] = 0;
    valid[n] = 1;

    // Sommes cumulées des longueurs de mots
    ll *prefix_sum = malloc(sizeof(ll) * n);
    prefix_sum[0] = p->words[0].len;
    for (int i = 1; i < n; i++)
        prefix_sum[i] = prefix_sum[i - 1] + p->words[i].len;

    // Boucle principale : calcul du coût optimal par Bellman itératif
    for (int i = n - 1; i >= 0; i--) {
        for (int k = i; k < n; k++) {
            ll d = delta(i, k, prefix_sum);
            if (d > M) break; // arrêt anticipé : ligne trop longue
            
            if (!valid[k + 1]) continue;
            
            ll penalty = (k == n - 1) ? 0 : cube(M - d);
            if (penalty < 0) penalty = LLONG_MAX;
            
            ll future_cost = dp[k + 1];
            if (future_cost == LLONG_MAX) continue;
            
            if (penalty > LLONG_MAX - future_cost) continue;
            
            ll total = penalty + future_cost;
            
            if (total < dp[i]) {
                dp[i] = total;
                next_break[i] = k + 1;
                valid[i] = 1;
            }
        }
    }

    ll cost = dp[0];
    int is_valid = valid[0];
    
    free(valid);
    free(dp);
    free(prefix_sum);
    
    if (!is_valid) {
        error("impossible de justifier le paragraphe");
    }
    
    return cost;
}

// =====================================================================
// Écriture d'une ligne justifiée selon les espaces à répartir
// =====================================================================
void write_justified_line(FILE *out, Word *words, int start, int end, ll total_spaces) {
    int word_count = end - start;
    if (word_count == 1) {
        // Un seul mot : pas de justification
        fwrite(words[start].start, 1, words[start].len, out);
        return;
    }

    ll base_spaces = total_spaces / (word_count - 1);
    ll extra_spaces = total_spaces % (word_count - 1);

    for (int j = start; j < end; j++) {
        fwrite(words[j].start, 1, words[j].len, out);
        if (j < end - 1) {
            // Distribution des espaces : uniformes + 1 si reste
            int spaces = base_spaces + (j - start < extra_spaces ? 1 : 0) + 1;
            for (int s = 0; s < spaces; s++) fputc(' ', out);
        }
    }
}

// =====================================================================
// Écriture complète d'un paragraphe justifié
// =====================================================================
void write_paragraph(FILE *out, Paragraph *p, ll M, int *next_break) {
    int i = 0, n = p->word_count;
    while (i < n) {
        int k = next_break[i];
        if (k <= i) error("impossible de justifier le paragraphe");

        // Dernière ligne non justifiée
        if (k == n) {
            for (int j = i; j < k; j++) {
                fwrite(p->words[j].start, 1, p->words[j].len, out);
                if (j < k - 1) fputc(' ', out);
            }
        } else {
            // Justification complète
            ll line_length = 0;
            for (int j = i; j < k; j++) line_length += p->words[j].len + 1;
            line_length--;
            write_justified_line(out, p->words, i, k, M - line_length);
        }

        if (k < n) fputc('\n', out);
        i = k;
    }
}

// =====================================================================
// Fonction pour créer le nom du fichier de sortie
// =====================================================================
char* create_output_filename(const char* input_filename) {
    static char output_filename[1024];
    
    // Chercher la dernière occurrence de ".in"
    const char* last_in = strstr(input_filename, ".in");
    
    if (last_in != NULL && strlen(last_in) == 3) {
        // Remplacer ".in" par ".out"
        size_t base_len = last_in - input_filename;
        strncpy(output_filename, input_filename, base_len);
        output_filename[base_len] = '\0';
        strcat(output_filename, ".out");
    } else {
        // Pas de ".in" à la fin, ajouter simplement ".out"
        strcpy(output_filename, input_filename);
        strcat(output_filename, ".out");
    }
    
    return output_filename;
}

// =====================================================================
// Fonction principale : lecture, traitement et sortie
// =====================================================================
int main(int argc, char *argv[]) {
    if (argc != 3)
        error("Usage: AODjustify M file.in");

    ll M = atoll(argv[1]);
    if (M <= 0 || M > MAX_LINE_LEN)
        error("Largeur M invalide");

    const char *filename = argv[2];
    validate_iso8859_1(filename);

    size_t size;
    char *data = map_file(filename, &size);

    Paragraph paragraphs[1000];
    int para_count = 0;
    extract_paragraphs(data, size, paragraphs, &para_count);
    if (para_count == 0)
        error("Aucun paragraphe detecte");

    // CRÉATION DU FICHIER DE SORTIE AVEC LE BON NOM
    char outname[1024];
    const char* last_in = strstr(filename, ".in");
    
    if (last_in != NULL && strlen(last_in) == 3) {
        // Remplacer ".in" par ".out"
        size_t base_len = last_in - filename;
        strncpy(outname, filename, base_len);
        outname[base_len] = '\0';
        strcat(outname, ".out");
    } else {
        // Pas de ".in" à la fin, ajouter simplement ".out"
        snprintf(outname, sizeof(outname), "%s.out", filename);
    }
    
    FILE *out = fopen(outname, "w");
    if (!out) error("Impossible de creer le fichier de sortie");

    ll total_cost = 0;
    int *next_break = malloc(sizeof(int) * MAX_WORDS);
    if (!next_break) error("Erreur d'allocation memoire");

    // Traitement de chaque paragraphe indépendamment
    for (int p_idx = 0; p_idx < para_count; p_idx++) {
        Paragraph *p = &paragraphs[p_idx];
        for (int i = 0; i < p->word_count; i++)
            if (p->words[i].len > M) {
                char error_msg[200];
                snprintf(error_msg, sizeof(error_msg),
                        "le fichier possède un mot de longueur supérieure à %lld caractères : justification sur %lld caractères impossible",
                        M, M);
                error(error_msg);
            }
        ll cost = justify_paragraph(p, M, next_break);
        total_cost += cost;
        write_paragraph(out, p, M, next_break);
        if (p_idx < para_count - 1) fprintf(out, "\n\n");
        free(p->words);
    }

    fclose(out);
    munmap(data, size);
    free(next_break);

    // Affiche le coût total sur la sortie d'erreur standard (spécification TP)
    fprintf(stderr, "AODjustify CORRECT> %lld\n", total_cost);
    return EXIT_SUCCESS;
}