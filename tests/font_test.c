#include <stdio.h>
#include <stdint.h>

#include "font.h"

static int failures = 0;

static void check(int condition, const char *message)
{
    if (!condition) {
        printf("[FAIL] %s\n", message);
        failures++;
    }
}

int main(void)
{
    int tested = 0;

    printf("ProZest OS - Font Unit Test\n");
    printf("==========================\n");

    for (int code = 32; code <= 126; code++) {
        const uint8_t *glyph = font_get_glyph((char)code);

        if (glyph == NULL) {
            printf("[FAIL] ASCII %d returned NULL\n", code);
            failures++;
            continue;
        }

        int has_pixel = 0;

        for (int row = 0; row < FONT_HEIGHT; row++) {
            if ((glyph[row] & ~0x1F) != 0) {
                printf("[FAIL] ASCII %d row %d exceeds 5 bits\n",
                       code, row);
                failures++;
            }

            if (glyph[row] != 0) {
                has_pixel = 1;
            }
        }

        if (code != 32 && !has_pixel) {
            printf("[FAIL] ASCII %d has an empty bitmap\n", code);
            failures++;
        }

        tested++;
    }

    /* Verify representative patterns and fallback behavior. */
    check(font_get_glyph('A')[0] == 14,
          "Uppercase A pattern");

    check(font_get_glyph('a')[2] == 14,
          "Lowercase a pattern");

    check(font_get_glyph('0')[0] == 14,
          "Digit zero pattern");

    check(font_get_glyph('!')[0] == 4,
          "Exclamation mark pattern");

    check(font_get_glyph(' ')[0] == 0,
          "Space is blank");

    check(font_get_glyph('\x7f') != NULL,
          "Unsupported character has fallback");

    printf("Characters checked: %d\n", tested);
    printf("Failures: %d\n", failures);

    if (failures == 0) {
        printf("[PASS] Printable ASCII font tests\n");
        return 0;
    }

    return 1;
}
