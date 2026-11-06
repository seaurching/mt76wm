/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_LINUX_CTYPE_H
#define MTK_LC_LINUX_CTYPE_H

#include <linux/mtk_core.h>

/* bit0 space, bit1 digit, bit2 alpha, bit3 upper, bit4 lower, bit5 xdigit */
#define __MTK_CTYPE_SPACE  0x001
#define __MTK_CTYPE_DIGIT  0x002
#define __MTK_CTYPE_ALPHA  0x004
#define __MTK_CTYPE_UPPER  0x008
#define __MTK_CTYPE_LOWER  0x010
#define __MTK_CTYPE_XDIGIT 0x020

static inline unsigned int mtk_ctype_bits(unsigned char c)
{
    unsigned int bits = 0;

    if (c == ' ' || (c >= '\t' && c <= '\r'))
        bits |= __MTK_CTYPE_SPACE;
    if (c >= '0' && c <= '9')
        bits |= __MTK_CTYPE_DIGIT | __MTK_CTYPE_XDIGIT;
    if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
        bits |= __MTK_CTYPE_XDIGIT;
    if (c >= 'a' && c <= 'z')
        bits |= __MTK_CTYPE_ALPHA | __MTK_CTYPE_LOWER;
    if (c >= 'A' && c <= 'Z')
        bits |= __MTK_CTYPE_ALPHA | __MTK_CTYPE_UPPER;
    return bits;
}

#define isalnum(c)  (mtk_ctype_bits((unsigned char)(c)) & (__MTK_CTYPE_ALPHA | __MTK_CTYPE_DIGIT))
#define isalpha(c)  (mtk_ctype_bits((unsigned char)(c)) & __MTK_CTYPE_ALPHA)
#define isdigit(c)  (mtk_ctype_bits((unsigned char)(c)) & __MTK_CTYPE_DIGIT)
#define isxdigit(c) (mtk_ctype_bits((unsigned char)(c)) & __MTK_CTYPE_XDIGIT)
#define isupper(c)  (mtk_ctype_bits((unsigned char)(c)) & __MTK_CTYPE_UPPER)
#define islower(c)  (mtk_ctype_bits((unsigned char)(c)) & __MTK_CTYPE_LOWER)
#define isspace(c)  (mtk_ctype_bits((unsigned char)(c)) & __MTK_CTYPE_SPACE)
#define isprint(c)  ((unsigned char)(c) >= 0x20 && (unsigned char)(c) < 0x7f)
#define isgraph(c)  ((unsigned char)(c) > 0x20 && (unsigned char)(c) < 0x7f)
#define iscntrl(c)  ((unsigned char)(c) < 0x20 || (unsigned char)(c) == 0x7f)

#define toupper(c) (((c) >= 'a' && (c) <= 'z') ? ((c) - 'a' + 'A') : (c))
#define tolower(c) (((c) >= 'A' && (c) <= 'Z') ? ((c) - 'A' + 'a') : (c))

#endif /* MTK_LC_LINUX_CTYPE_H */
