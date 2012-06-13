#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int
strcmpic(const char *s1, const char *s2)
{
    if (!s1) {
        s1 = "";
    }

    if (!s2) {
        s2 = "";
    }

    while (*s1 && *s2) {
        if ((*s1 != *s2) && (tolower(*s1) != tolower(*s2))) {
            break;
        }
        s1++, s2++;
    }

    return (tolower(*s1) - tolower(*s2));
}


int
strncmpic(const char *s1, const char *s2, size_t n)
{
    if (n <= (size_t)0) {
        return(0);
    }

    if (!s1) {
        s1 = "";
    }

    if (!s2) {
        s2 = "";
    }

    while (*s1 && *s2) {
        if ((*s1 != *s2) && (tolower(*s1) != tolower(*s2))) {
            break;
        }

        if (--n <= (size_t)0) {
            break;
        }

        s1++, s2++;
    }

    return (tolower(*s1) - tolower(*s2));
}
