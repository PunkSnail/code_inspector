
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>     /* access ... */
#include <sys/stat.h>   /* stat */
#include <regex.h>      /* regcomp ... */
#include <ctype.h>      /* isdigit */

#include <vector>
#include <list>
#include <map>
#include <string>
#include <fstream>      /* filebuf ... */
#include <iostream>

#include "code_inspector.h"

using namespace std;

#define NORMAL_PROCESS_REG      "while\\(."
#define SINGLE_PROCESS_REG      "while\\(.+.&&.+>0\\)"

#define show(...) printf(__VA_ARGS__);

#define show_red(fmt, args...) \
{ \
    printf("\033[1;31;1m" fmt "\033[m",  ##args); \
}

#define show_green(fmt, args...) \
{ \
    printf("\033[1;32;1m" fmt "\033[m",  ##args); \
}

/* don't care about capital (c >='A' && c <= 'Z') */
#define IS_VAR(c) \
    (((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') ? \
     true : false)

#define IN_ALPHABET(c) \
    (((c >= 'a' && c <= 'z') || (c >='A' && c <= 'Z')) ? true : false)


typedef struct
{
    uint32_t arr[MULTI_FLOW_ARR_SIZE];

}multi_start_t;

typedef struct
{
    uint32_t single_start;
    uint32_t multi_start;
    uint32_t left;
    uint32_t right;
    uint32_t m_left;
    uint32_t m_right;
    multi_start_t multi_record;

}loop_helper_t;

struct code_inspector_t
{
public:
    code_inspector_t();
    ~code_inspector_t();

    void clear_refers_arr(void);

    bool is_perfect_match;
    vector <string> code_vec;
    vector <string> format_code_vec;

    /* start and end line numbers */
    list <pair<uint32_t, uint32_t>> single_list;
    list <pair<uint32_t, uint32_t>> multi_list;

    /* multi start related to single single */
    map <uint32_t, multi_start_t> related_map; 

    /* used to record match reference count */
    uint32_t *refers_arr;
    uint32_t arr_size;
};

code_inspector_t::code_inspector_t()
{
    this->is_perfect_match = false;
    /* align index and line number */
    this->code_vec.push_back("stub");

    this->refers_arr = NULL;
    this->arr_size = 0;
}

code_inspector_t::~code_inspector_t()
{
    if (this->refers_arr)
    {
        delete this->refers_arr;
        this->refers_arr = NULL;
        this->arr_size = 0;
    }
}

void code_inspector_t::clear_refers_arr(void)
{
    memset(this->refers_arr, 0, this->arr_size * sizeof(uint32_t));
}

static bool is_invalid_param(const char *code_path)
{
    bool result = true;
    struct stat st;

    if (NULL == code_path)
    {
        show_red("the code path is empty\n");
        return result;
    }
    if (access(code_path, F_OK) == -1)
    {
        show_red("access code file failed: %s\n", code_path);
        return result;
    }
    stat(code_path, &st);
    if (S_ISDIR(st.st_mode))
    {
        show_red("the code path is a directory: %s\n", code_path);
        return result;
    }
    if (st.st_size > MAXIMUM_FILE_SIZE)
    {
        show_red("code file too big, current limit: %ub\n", MAXIMUM_FILE_SIZE);
        return result;
    }
    result = false;

    return result;
}

static int load_code_file(const char *open_path, code_inspector_t *p_coder)
{
    int result = -1;
    filebuf fb;
    string line;

    if(fb.open(open_path, ios::in) == NULL)
    {
        show_red("open error: %s\n", strerror(errno));
        return result;
    }
    istream file_stream(&fb);

    while (getline(file_stream, line, '\n'))
    {
        p_coder->code_vec.push_back(line);
    }
    fb.close();

    result = 0;
    return result;
}

/* when using functions of the string class,
 * don't need to care about crossing the boundary */
static void ignore_comments(string *p_line, bool *p_comment_judge, 
                            bool *p_macro_judge, int *p_if_count)
{
    /* first deal with comment symbol */
    if (p_line->compare(0, 2, "//") == 0)
    {
        p_line->clear();
        return;
    }
    if (p_line->compare(0, 2, "/*") == 0 )
    {
        *p_comment_judge = true;
    }
    /* comment between symbols */
    if (*p_comment_judge && p_line->find("*/") == string::npos)
    {
        p_line->clear();
        return;
    }
    else /* normal case */
    {
        /* clear lines ending with comment symbol */
        if (*p_comment_judge && p_line->size() >= 2 
            &&  '*' == p_line->c_str()[p_line->size() - 2]
            &&  '/' == p_line->c_str()[p_line->size() - 1])
        {
            p_line->clear();
        }
        *p_comment_judge = false;
    }
    /* handling nested #if */
    if (p_line->compare(0, 4, "#if0") == 0)
    {
        *p_macro_judge = true; 
    }
    if (*p_macro_judge)
    {
        if (p_line->compare(0, 3, "#if") == 0)
        {
            (*p_if_count)++;
        }
        if (p_line->compare(0, 6, "#endif") == 0
            && (--(*p_if_count) == 0))
        {
            *p_macro_judge = false;
        }
        p_line->clear();
    }
}

/* ignore unassigned variables */
static void ignore_unassigned(string *p_line, bool is_unassigned)
{
    if (is_unassigned && !p_line->empty()
        && p_line->compare(0, 4, "goto")
        && p_line->compare(0, 4, "else")
        && p_line->compare(0, 5, "break")
        && p_line->compare(0, 6, "return")
        && p_line->compare(0, 8, "continue"))
    {
        p_line->clear();
    }
}

static void ignore_special(string *p_line, 
                           const char **ignore_arr, uint32_t arr_len)
{
    for (size_t i = 0; i < arr_len; i++)
    {
        if (p_line->find(ignore_arr[i]) != string::npos)
        {
            p_line->clear();
            return;
        }
    }
}

static void format_code_string(vector<string> &format_code_vec)
{
    string *p_line;
    size_t i;
    bool is_unassigned;

    bool comment_judge = false;
    bool macro_judge = false;
    int if_count = 0;   /* between #if 0  and #endif */

    /* "line_no + 1" exists inside the loop, so here size() - 1 */
    for (size_t line_no = 1; line_no < format_code_vec.size() - 1; line_no++)
    {
        p_line = &format_code_vec[line_no];
        is_unassigned = true;
re_loop:
        /* must remove spaces first */
        for (i = 0; i < p_line->size(); )
        {
            char ch = p_line->c_str()[i];

            if (' ' ==  ch || ';' == ch || '\r' == ch || '\n' == ch)
            {
                p_line->erase(i, 1);
                continue;
            }
            if (is_unassigned && !IS_VAR(ch) && ',' != ch && '*' != ch)
            {
                is_unassigned = false;
            }
            i++;
        }
        /* end whit '=' or can't find match ')',  merge next line */
        if (i && ('=' == p_line->c_str()[i - 1] ||
                  (p_line->find('(') != string::npos
                   && p_line->find(')') == string::npos)))
        {
            *p_line += format_code_vec[line_no + 1];
            format_code_vec[line_no + 1].clear();

            line_no++;
            goto re_loop;
        }
        ignore_comments(p_line, &comment_judge, &macro_judge, &if_count);

        ignore_unassigned(p_line, is_unassigned);

        ignore_special(p_line, g_ignore_arr, 
                       sizeof(g_ignore_arr) / sizeof(char*));
    }
}

static bool match_assign_range(list <pair<uint32_t, uint32_t>> &range_list, 
                               uint32_t first_brace, uint32_t last_brace)
{
    for (list <pair<uint32_t, uint32_t>>::iterator it = range_list.begin(); 
         it != range_list.end(); it++)
    {
        /* line number minus line number, fuzzy matching 0, 1, 2 */
        if (first_brace - it->first < 3)
        {
            it->second = last_brace;
            return true;
        }
    }
    return false;
}

static bool reg_judge_format(const char *pattern, const char *source)
{
    bool result = false;

    regex_t regex;
    memset(&regex, 0, sizeof(regex_t));

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        show_red("regcomp error: %s\n", pattern);
        return result;
    }
    if (regexec(&regex, source, 0, NULL, 0) == 0)
    {
        result = true;
    }
    regfree(&regex);
    
    return result;
}

static bool filter_multi_flow(string *p_line, uint32_t line_no, uint32_t *arr)
{
    /* the order is important */
    if (p_line->find('2') != string::npos)
    {
        arr[MULTI_FLOW_2] = line_no;
    }
    else if (p_line->find('4') != string::npos)
    {
        arr[MULTI_FLOW_4] = line_no;
    }
    else if (p_line->find('8') != string::npos)
    {
        arr[MULTI_FLOW_8] = line_no;
    }
    else if (p_line->find("16") != string::npos)
    {
        arr[MULTI_FLOW_16] = line_no;
    }
    else {
        return false;
    } 
    return true;
}

static void deal_with_line(code_inspector_t *p_coder, 
                           string *p_line, uint32_t i, loop_helper_t *p_h)
{
    list <pair<uint32_t, uint32_t>> &single_list = p_coder->single_list;
    list <pair<uint32_t, uint32_t>> &multi_list = p_coder->multi_list;

    if (p_line->find('{') != string::npos)
    {
        p_h->left++;
        p_h->m_left++;
    }
    if (p_line->find('}') != string::npos)
    {
        p_h->right++;
        p_h->m_right++;
        /* assign value to the end line number */
        if (p_h->single_start && 0 == p_h->right - p_h->left)
        {
            match_assign_range(single_list, p_h->single_start, i);
            p_h->single_start = 0;
        }
        else if (p_h->multi_start && 0 == p_h->m_right - p_h->m_left)
        {
            match_assign_range(multi_list, p_h->multi_start, i);
            p_h->multi_start = 0;
        }
    }
    if (false == reg_judge_format(NORMAL_PROCESS_REG, p_line->c_str()))
    {
        return;
    }
    /* multiple process */
    if (p_line->find('0') == string::npos)
    {
        if (filter_multi_flow(p_line, i, p_h->multi_record.arr))
        {
            /* don't know the end line number yet */
            multi_list.push_back(make_pair(i, 0));
            p_h->multi_start = i;
            p_h->m_left = p_h->m_right = 0;
        }
    }
    /* single process */
    else //if (reg_judge_format(SINGLE_PROCESS_REG, p_line->c_str()))
    {
        single_list.push_back(make_pair(i, 0));
        p_coder->related_map.insert(make_pair(i, p_h->multi_record));

        p_h->single_start = i;
        p_h->left = p_h->right = 0;
        memset(&p_h->multi_record, 0, sizeof(multi_start_t));
    }
}

static bool matching_multi(vector <string> &format_code_vec, 
                           list <pair<uint32_t, uint32_t>> &multi_list, 
                           uint32_t line_no)
{
    uint32_t last_brace = 0;
    uint32_t left = 0;
    uint32_t right = 0;
 
    for (uint32_t i = line_no; i > 0; i--)
    {
        if (0 == format_code_vec[i].size()) {
            continue;
        }
        if (format_code_vec[i].find('}') != string::npos)
        {
            if (0 == last_brace)
            {
                last_brace = i;
            }
            right++;
        }
        if (format_code_vec[i].find('{') != string::npos)
        {
            if (0 == last_brace)
            {
                return false;
            }
            left++;

            if (0 == right - left
                && match_assign_range(multi_list, i, last_brace))
            {
                return true;
            }
        }
    }
    return false;
}

static int find_key_lines(code_inspector_t *p_coder)
{
    int result = 0;

    vector <string> &format_code_vec = p_coder->format_code_vec;

    list <pair<uint32_t, uint32_t>>::iterator it;

    list <pair<uint32_t, uint32_t>> &single_list = p_coder->single_list;
    list <pair<uint32_t, uint32_t>> &multi_list = p_coder->multi_list;

    loop_helper_t helper;
    memset(&helper, 0, sizeof(loop_helper_t));

    for (uint32_t i = 0; i < (uint32_t)format_code_vec.size(); i++)
    {
        if (0 == format_code_vec[i].size()) {
            continue;
        }
        deal_with_line(p_coder, &format_code_vec[i], i, &helper);
    }
    /* erase those invalid range */
    for (it = single_list.begin(); it != single_list.end(); )
    {
        if (0 == it->second ||
            !matching_multi(format_code_vec, multi_list, it->first))
        {
            /* don't worry, you can always find it in this case. */
            p_coder->related_map.erase(p_coder->related_map.find(it->first));

            single_list.erase(it++);
        }
        else {
            it++;
        }
    }
    if (multi_list.empty() || single_list.empty())
    {
        show_red("the specified line couldn't be found\n");
    }
    return result;
}

static void replace_zore(string *p_line, int no)
{
    for (size_t i = 1; i < p_line->size(); i++)
    {
        char ch = p_line->c_str()[i - 1];

        if ('0' == p_line->c_str()[i] && IS_VAR(ch))
        {
            p_line->replace(i, 1, to_string(no));
        }
    }
}

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
        replace_zore(&split, i);
        *p_line = *p_line + delim + split;
    }
}

/*  match "x0[n];" with "xn[n];" */
static bool match_multi_line(string *single, string *multi, int n)
{
    string intrm;

    if (multi->compare(*single) == 0) 
    {
        return true;
    }
    for (int i = 1; i < n; i++)
    {
        intrm = *single;

        if (multi->find('0') != string::npos)
        {
            replace_var_zore(&intrm, i);
        }
        else {
            replace_num(&intrm, '0', i);
        }
        if (multi->compare(intrm) == 0)
        {
            return true;
        }
    }
    return false;
}

/*  match "func(x0 & x1 & y0 & y1);" with "func(x0 & y0);" */
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

/*  match "x0 = x1 = n;" with "x0 = n;" */
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

/* match "x += n;" with "x += 1;" */
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

/* match "func_xn(a, b0, bn, c0, cn)" with "func_x1(a, b0, c0)" */
static bool match_multi_func(string *single, string *multi, int n)
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
            //if (',' == single->c_str()[j]) {
            if (!IS_VAR(single->c_str()[j])) {
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

static void compare_with_single(code_inspector_t *p_coder, multi_type_t type,
                                pair<uint32_t, uint32_t> *s_range, 
                                pair<uint32_t, uint32_t> *m_range)
{
    string *single;
    string *multi;
    bool is_match = false;
    int n = g_multi_num_arr[type];

    /* range start +1 skip the "while" line */
    for (uint32_t i = m_range->first + 1; i < m_range->second; i++)
    {
        multi = &p_coder->format_code_vec[i];
        /* ignore empty line or symbol */
        if (multi->size() < 2) {
            continue;
        }
        for (uint32_t j = s_range->first + 1; j < s_range->second; j++)
        {
            single = &p_coder->format_code_vec[j];

            /* this line has been matched multiple times or invalid length */
            if (p_coder->refers_arr[j] >= (uint32_t)n 
                || single->size() < 2 || multi->size() < single->size())
            {
                continue;
            }
            if (match_multi_line(single, multi, n)
                || match_multi_var(single, multi, n)
                || match_multi_equal(single, multi, n)
                || match_multi_calc(single, multi, n)
                || match_multi_func(single, multi, n))
            {
                is_match = true;
                p_coder->refers_arr[j]++;
                break;
            }
        }
        if (false == is_match)
        {
            p_coder->is_perfect_match = false;
            show_red("%u%s\n", i, p_coder->code_vec[i].c_str());
        }
        is_match = false;
    }
}

static void pick_multi_to_compare(code_inspector_t *p_coder, 
                                  multi_type_t type, uint32_t start, 
                                  pair<uint32_t, uint32_t> *s_range)
{
    list <pair<uint32_t, uint32_t>>::iterator multi_it;

    for (multi_it = p_coder->multi_list.begin(); 
         start && multi_it != p_coder->multi_list.end(); multi_it++)
    {
        if (multi_it->first != start) {
            continue;
        }
        show("MULTI FLOW %d START %u\n", g_multi_num_arr[type], start);
        p_coder->clear_refers_arr();

        compare_with_single(p_coder, type, s_range, &(*multi_it));

        if (p_coder->is_perfect_match)
        {
            show_green("\tis perfect match\n");
        }
        /* XXX: Now I know which lines in the single process don't match, 
         * maybe try to do something */
        break;
    }
}

static void code_flow_analysis(code_inspector_t *p_coder)
{
    list <pair<uint32_t, uint32_t>>::iterator single_it;

    map <uint32_t, multi_start_t>::iterator map_it;

    single_it = p_coder->single_list.begin();

    for (; single_it != p_coder->single_list.end(); single_it++)
    {
        map_it = p_coder->related_map.find(single_it->first);

        for (int i = 0; i < MULTI_FLOW_ARR_SIZE; i++)
        {
            p_coder->is_perfect_match = true;

            pick_multi_to_compare(p_coder, (multi_type_t)i, 
                                  map_it->second.arr[i], &(*single_it));
        }
    }
}

static int inspector_start_work(code_inspector_t *p_coder)
{
    int result = -1;
    p_coder->format_code_vec = p_coder->code_vec;
    
    format_code_string(p_coder->format_code_vec);

    result = find_key_lines(p_coder);

    if (0 != result)
    {
        return result;
    }
    code_flow_analysis(p_coder);
    
    return result;
}

int code_inspector_input(const char *code_path)
{
    int result = -1;
    code_inspector_t *p_coder = NULL;

    if (is_invalid_param(code_path)) {
        goto done;
    }
    show("code path: ");
    show_green("%s\n", code_path);

    p_coder = new code_inspector_t;

    if (0 != load_code_file(code_path, p_coder))
    {
        show_red("error reading file into memory: %s\n", strerror(errno));
        goto done;
    }
    p_coder->arr_size = (uint32_t)p_coder->code_vec.size();
    p_coder->refers_arr = new uint32_t[p_coder->arr_size];

    p_coder->clear_refers_arr();

    result = inspector_start_work(p_coder);
done:
    if (p_coder)
    {
        delete p_coder;
        p_coder = NULL;
    }
    return result;
}

