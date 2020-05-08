
#include <string>
#include <iostream>

#include "code_inspector.h"
#include "matching_helper.h"

using namespace std;

/* when these functions are called, the string has been formatted
 * ' ', '\r' and '\n' are removed.
 */

static void replace_var_zore(string *p_line, int no)
{
    for (size_t i = 1; i < p_line->size() - 1; i++)
    {
        char ch1 = p_line->c_str()[i - 1];
        char ch2 = p_line->c_str()[i + 1];

        if ('0' == p_line->c_str()[i] && IN_ALPHABET(ch1) && !IS_VAR(ch2))
        {
            p_line->replace(i, 1, to_string(no));
        }
    }
}

static bool replace_num(string *p_line, const char ch, int no)
{
    bool result = false;
    for (size_t i = 1; i < p_line->size(); i++)
    {
        if (ch == p_line->c_str()[i])
        {
            result = true;
            p_line->replace(i, 1, to_string(no));
        }
    }
    return result;
}

static void multi_stitch(string *p_line,
                         string *p_base, int limit, const char *delim)
{
    string split;

    for (int i = 1; i < limit; i++)
    {
        split = *p_base;

        for (size_t j = 1; j < split.size(); j++)
        {
            char ch = split.c_str()[j - 1];

            if ('0' == split.c_str()[j] && IS_VAR(ch))
            {
                split.replace(j, 1, to_string(i));
            }
        }
        *p_line = *p_line + delim + split;
    }
}

/* match "a1f=0;" with "a0f=0;" */
static bool match_multi_line(const format_item_t *single_item,
                             string *multi, int n)
{
    string intrm;

    string single = single_item->line;
    int refer = single_item->refer_count;
    size_t pos = single.find('0');

    /* the first time match */
    if ((0 == refer || string::npos == pos)
        && multi->compare(single) == 0)
    {
        return true;
    }
    if (refer >= n || string::npos == pos)
    {
        return false;
    }
    intrm = single;
    intrm.replace(pos, 1, to_string(refer));
    if (multi->compare(intrm) == 0)
    {
        return true;
    }
    /* match "a1=b1->h+func(0x10);" with "a0=b0->h+func(0x10);" */
    intrm = single;
    if (multi->find('0') != string::npos)
    {
        replace_var_zore(&intrm, refer);
    }
    else {
        replace_num(&intrm, '0', refer);
    }
    if (multi->compare(intrm) == 0)
    {
        return true;
    }
    return false;
}

/*  match "func(x0&x1&y0&y1);" with "func(x0&y0);" */
static bool match_multi_var(string *single, string *multi, int n)
{
    bool result = false;
    string intrm, symbol, del;
    size_t pos;
    char ch;

    intrm = *multi;
    pos = multi->rfind('(');

    if (pos == string::npos)
    {
        return result;
    }
    for (size_t i = pos, j = 0; i < single->size() - 1; i++)
    {
        if ('0' != single->c_str()[i]) {
            continue;
        }
        for (j = i; j > 1; j--)
        {
            if (!IS_VAR(single->c_str()[j]))
            {
                j++;
                break;
            }
        }
        for (size_t k = 1; i > j && k < (size_t)n; k++)
        {
            ch = multi->c_str()[i + 1];

            if (symbol.empty() &&
                !IS_VAR(ch) && ch == multi->c_str()[i + 2]) // such as "&&"
            {
                symbol.assign(multi->c_str() + i + 1, 2);
            }
            else if (symbol.empty() && !IS_VAR(ch)) // such as "&"
            {
                symbol.assign(multi->c_str() + i + 1, 1);
            }
            del = symbol;
            del.append(single->c_str() + j, i - j);
            del += to_string(k);

            if (symbol.empty() || string::npos == (pos = intrm.find(del)))
            {
                return result;
            }
            intrm.erase(pos, del.size());
            del.clear();
        }
    }
    if (intrm.compare(*single) == 0)
    {
        result = true;
    }
    return result;
}

/*  match "x0=x1=n;" with "x0=n;" */
static bool match_multi_equal(string *single, string *multi, int n)
{
    int equal_count = 0;

    string base;
    size_t offset = 0;
    string intrm;

    for (size_t i = 0; i < multi->size(); i++)
    {
        if ('=' == multi->c_str()[i])
        {
            if (0 == offset) {
                offset = i;
            }
            equal_count++;
        }
    }
    if (equal_count > 1)
    {
        base.assign(single->c_str(), offset);
        intrm = base;
    }
    else {
        return false;
    }
    multi_stitch(&intrm, &base, equal_count, "=");

    if (multi->compare(0, intrm.size(), intrm.c_str()) == 0)
    {
        return true;
    }
    return false;
}

/* match "x+=n;" with "x+=1;" */
static bool match_multi_calc(string *single, string *multi, int n)
{
    bool result = false;
    string intrm;

    if (single->find("+=1") == string::npos &&
        single->find("-=1") == string::npos)
    {
        return result;
    }
    intrm = *single;

    replace_num(&intrm, '1', n);
    if (multi->compare(0, intrm.size(), intrm.c_str()) == 0)
    {
        return true;
    }
    return result;
}

/* match "func_xn(a,b0,bn,c0,cn);" with "func_x1(a,b0,c0);" */
static bool match_multi_func(const string *single, const string *multi, int n)
{
    bool result = false;
    string intrm, del;
    size_t pos;

    intrm = *multi;
    if (false == replace_num(&intrm, to_string(n)[0], 1)) {
        return result;
    }
    for (size_t i = 0, j = 0; i < single->size(); i++)
    {
        if ('0' != single->c_str()[i]) {
            continue;
        }
        for (j = i; j > 1; j--)
        {
            if (',' == single->c_str()[j]) {
                break;
            }
        }
        for (size_t k = 1; k < (size_t)n; k++)
        {
            del.assign(single->c_str() + j, i - j);
            del += to_string(k);

            if (string::npos == (pos = intrm.find(del)))
            {
                return result;
            }
            intrm.erase(pos, del.size());
        }
    }
    if (intrm.compare(*single) == 0)
    {
        result = true;
    }
    return result;
}

bool varied_matching_rules(const format_item_t *single,
                           const char *multi, int n)
{
    bool result = false;
    string single_str = single->line;
    string multi_str = multi;

    if (match_multi_line(single, &multi_str, n)
        || match_multi_var(&single_str, &multi_str, n)
        || match_multi_equal(&single_str, &multi_str, n)
        || match_multi_calc(&single_str, &multi_str, n)
        || match_multi_func(&single_str, &multi_str, n))
    {
        result = true;
    }
    return result;
}

