// FoloTalker 多行输入表单的纯逻辑 —— 主机可测。
//
// 界面上有两种多行表单:
//   - 服务器地址页:地址 / 保存
//   - 门禁页:昵称 / 口令 / 保存
//
// 上/下键在行之间移动,确定键在文本行输入字符、在"保存"行提交。
// 切行时要把当前编辑框的内容落到对应字段,否则刚输入的字符会丢。
//
// 把这个逻辑从 LVGL 里抽出来,是为了能在主机上验证"切行不丢字"这条
// 容易出错、又不容易在真机上察觉的规则。
#pragma once

#include "ft_textfield.h"

#ifdef __cplusplus
extern "C" {
#endif

// 表单的行。
typedef enum {
    FT_FORM_ROW_TEXT = 0,   // 文本输入行
    FT_FORM_ROW_SAVE = 1,   // 保存行
} ft_form_row_kind_t;

// 一个多行表单。
typedef struct {
    ft_text_field_t fields[3];   // 各行的输入框
    ft_form_row_kind_t kinds[3]; // 各行的类型
    int row_count;               // 行数
    int current;                 // 当前行
} ft_form_t;

// 初始化一个表单。
// rows: 每行的类型;count: 行数。
void ft_form_init(ft_form_t *form, const ft_form_row_kind_t *rows, int count);

// 当前行。
int ft_form_current(const ft_form_t *form);

// 当前行是不是"保存"行。
bool ft_form_on_save_row(const ft_form_t *form);

// 切到下一行(向上/向下)。返回新的行号。
// 切行时会把当前行已输入的内容提交到 fields[]。
int ft_form_move(ft_form_t *form, int delta);

// 取某一行已提交的内容。
const char *ft_form_value(const ft_form_t *form, int row);

// 取当前正在编辑的输入框(用于输入字符)。
ft_text_field_t *ft_form_current_field(ft_form_t *form);

// 在当前行输入一个字符。保存行不输入任何东西。
bool ft_form_type(ft_form_t *form, char c);

// 在当前行退格。
bool ft_form_backspace(ft_form_t *form);

#ifdef __cplusplus
}
#endif
