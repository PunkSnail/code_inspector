
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

#define show(...) \
{ \
    printf(__VA_ARGS__); \
}

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
    (((c >= 'a' && c <= 'z') || (c >='A' && c <= 'Z') || c == '_') ? \
     true : false)


typedef struct
{
    uint32_t multi_arr[MULTI_FLOW_ARR_SIZE];

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

    bool is_perfect_match;
    vector <string> code_vec;
    vector <string> format_code_vec;

    /* start and end line numbers */
    list <pair<uint32_t, uint32_t>> single_list;
    list <pair<uint32_t, uint32_t>> multi_list;

    /* multi start related to single single */
    map <uint32_t, multi_start_t> related_map; 
};

code_inspector_t::code_inspector_t()
{
    this->is_perfect_match = false;
    /* align index and line number */
    this->code_vec.push_back("stub");
}

code_inspector_t::~code_inspector_t() {  }

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

    for (size_t line_no = 1; line_no < format_code_vec.size() - 1; line_no++)
    {
        p_line = &format_code_vec[line_no];
        is_unassigned = true;
re_loop:
        /* must remove spaces first */
        for (i = 0; i < p_line->size(); )
        {
            char ch = p_line->c_str()[i];

            if (' ' ==  ch || ';' == ch)
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
        /* end whit '=', merge next line */
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

        //cout << *p_line << endl;
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

static bool filter_multi_flow(string *p_line, 
                              uint32_t line_no, uint32_t *multi_arr)
{
    /* the order is important */
    if (p_line->find('2') != string::npos)
    {
        multi_arr[MULTI_FLOW_2] = line_no;
    }
    else if (p_line->find('4') != string::npos)
    {
        multi_arr[MULTI_FLOW_4] = line_no;
    }
    else if (p_line->find('8') != string::npos)
    {
        multi_arr[MULTI_FLOW_8] = line_no;
    }
    else if (p_line->find("16") != string::npos)
    {
        multi_arr[MULTI_FLOW_16] = line_no;
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
        if (filter_multi_flow(p_line, i, p_h->multi_record.multi_arr))
        {
            /* don't know the end line number yet */
            multi_list.push_back(make_pair(i, 0));
            p_h->multi_start = i;
            p_h->m_left = p_h->m_right = 0;
        }
        else {
            /* show_red("invalid start line: %u:%s\n", */
            /*          i, p_coder->code_vec[i].c_str()); */
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

static void replace_num(string *p_line, const char ch, int no)
{
    for (size_t i = 1; i < p_line->size(); i++)
    {
        if (ch == p_line->c_str()[i]) 
        {
            p_line->replace(i, 1, to_string(no));
        }
    }
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

static bool drop_brackets(string *p_line)
{
    size_t found;

    found = p_line->rfind('(');

    if (found == string::npos)
    {
        return false;
    }
    else {
        *p_line = p_line->c_str() + found + 1;
    }
    found = p_line->find(')');

    if (found == string::npos)
    {
        return false;
    }
    else {
        p_line->assign(p_line->c_str(), found);
    }
    return true;
}

/*  match "x0[n];" with "xn[n];" */
static bool match_multi_line(string *single, 
                             string *multi, int multi_num)
{
    string middle;

    if (multi->compare(*single) == 0) 
    {
        return true;
    }
    for (int i = 1; i < multi_num; i++)
    {
        middle = *single;

        if (multi->find('0') != string::npos)
        {
            replace_var_zore(&middle, i);
        }
        else {
            replace_num(&middle, '0', i);
        }
        if (multi->compare(middle) == 0) 
        {
            return true;
        }
    }
    return false;
}

/*  match "func(x0 & x1 & y0 & y1);" with "func(x0 & y0);" */
static bool match_multi_var(string *single, 
                            string *multi, int multi_num)
{
    bool result = false;
    
    string var1, var2;
    string middle;
    string multi_copy;
    size_t found;
    char ch;

    found = multi->rfind('(');
    /* "MULTI_FLOW_16" case, need someone to check */
    if (multi_num > 8 || single->find('0') == string::npos
        || multi->find('0') == string::npos || found == string::npos)
    {
        return result;
    }
    middle = *single;
    multi_copy = *multi;
    if (strncmp(multi->c_str(), single->c_str(), found) != 0 ||
        !drop_brackets(&middle) || !drop_brackets(&multi_copy))
    {
        return result;
    }
    for (size_t i = 1; i < multi_copy.size() - 1; i++)
    {
        ch = multi_copy.c_str()[i];
        if (!IS_VAR(ch) && ch == multi_copy.c_str()[i + 1])
        {
            var1 = middle;
            string tmp(2, ch);
            multi_stitch(&middle, &var1, multi_num, tmp.c_str());
            goto end_compare;
        }
    }
    ch = '\0';
    for (size_t i = 1; i < middle.size() - 1; i++)
    {
        if (!IS_VAR(middle.c_str()[i]))
        {
            ch = middle.c_str()[i];

            var1.assign(middle.c_str(), i);
            var2 = middle.c_str() + i + 1;
            middle = var1;
            break;
        }
    }
    if ('\0' == ch) {
        return result;
    }
    multi_stitch(&middle, &var1, multi_num, &ch);
    middle += (&ch + var2);
    multi_stitch(&middle, &var2, multi_num, &ch);
end_compare:
    if (multi_copy.compare(middle) == 0)
    {
        result = true;
    }
    return result;
}

/*  match "x0 = x1 = n;" with "x0 = n;" */
static bool match_multi_equal(string *single, string *multi)
{
    int equal_count = 0;

    string base;
    size_t base_len = 0;
    string middle;

    for (size_t i = 0; i < multi->size(); i++)
    {
        if ('=' == multi->c_str()[i]) 
        {
            if (0 == base_len) {
                base_len = i;
            }
            equal_count++;
        }
    }
    if (equal_count > 1)
    {
        base.assign(single->c_str(), base_len);
        middle = base;
    }
    else {
        return false;
    }
    multi_stitch(&middle, &base, equal_count, "=");

    if (multi->compare(0, middle.size(), middle.c_str()) == 0)
    {
        return true;
    }
    return false;
}

/* match "x += 1;" with "x += n;" */
static bool match_multi_calc(string *single, string *multi, int multi_num)
{
    bool result = false;
    string middle;
    
    if (single->find("+=1") == string::npos &&
        single->find("-=1") == string::npos)
    {
        return result;
    }
    middle = *single;

    replace_num(&middle, '1', multi_num);
    if (multi->compare(0, middle.size(), middle.c_str()) == 0)
    {
        return true;
    }
    return result;
}

static void compare_with_single(code_inspector_t *p_coder, multi_type_t type,
                                pair<uint32_t, uint32_t> single_range, 
                                pair<uint32_t, uint32_t> multi_range)
{
    string *single;
    string *multi;
    vector <string> &format_code_vec = p_coder->format_code_vec;
    /* +1 skip while line */
    uint32_t single_idx = single_range.first + 1;

    bool is_match = false;
    int multi_num = g_multi_num_arr[type];

    for (uint32_t i = multi_range.first + 1; i < multi_range.second; i++)
    {
        multi = &format_code_vec[i];
        /* ignore empty line or symbol */
        if (multi->size() < 2) {
            continue;
        }
        for (uint32_t j = single_idx; j < single_range.second && !is_match; j++)
        {
            single = &format_code_vec[j];

            if (single->size() < 2 || multi->size() < single->size()) {
                continue;
            }
            if (match_multi_line(single, multi, multi_num))
            {
                is_match = true;
            }
            else if (match_multi_var(single, multi, multi_num))
            {
                is_match = true;
                single_idx++;
            }
            else if (match_multi_equal(single, multi))
            {
                is_match = true;
                single_idx++;
            }
            else if (match_multi_calc(single, multi, multi_num))
            {
                is_match = true;
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

static void code_flow_analysis(code_inspector_t *p_coder)
{
    list <pair<uint32_t, uint32_t>>::iterator single_it;
    list <pair<uint32_t, uint32_t>>::iterator multi_it;

    list <pair<uint32_t, uint32_t>> &single_list = p_coder->single_list;
    list <pair<uint32_t, uint32_t>> &multi_list = p_coder->multi_list;

    uint32_t start_line = 0;
    map <uint32_t, multi_start_t>::iterator map_it;

    single_it = single_list.begin();

    while (single_it != single_list.end())
    {
        map_it = p_coder->related_map.find(single_it->first);

        for (int i = 0; i < MULTI_FLOW_ARR_SIZE; i++)
        {
            start_line = map_it->second.multi_arr[i];

            p_coder->is_perfect_match = true;

            for (multi_it = multi_list.begin(); 
                 start_line && multi_it != multi_list.end(); multi_it++)
            {
                if (multi_it->first != start_line) {
                    continue;
                }
                show("MULTI FLOW %d START %u\n", g_multi_num_arr[i], start_line);

                compare_with_single(p_coder, (multi_type_t)i, *single_it, *multi_it);

                if (p_coder->is_perfect_match) {
                    show_green("\tis perfect match\n");
                }
                break;
            }
        }
        single_it++;
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
    result = inspector_start_work(p_coder);
done:
    if (p_coder)
    {
        delete p_coder;
        p_coder = NULL;
    }
    return result;
}

