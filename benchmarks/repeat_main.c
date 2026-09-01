#include <stdlib.h>

extern int flow_program_main(void);

int main(void) {
    const char *text = getenv("FLOW_BENCH_REPS");
    char *end = NULL;
    unsigned long repetitions = text == NULL ? 1 : strtoul(text, &end, 10);
    if (text != NULL && (end == text || *end != '\0')) return EXIT_FAILURE;
    if (repetitions == 0) repetitions = 1;
    for (unsigned long i = 0; i < repetitions; ++i)
        if (flow_program_main() != 0) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
