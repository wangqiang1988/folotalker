// FoloTalker 多行输入表单的实现。
#include "ft_form.h"

#include <string.h>

void ft_form_init(ft_form_t *form, const ft_form_row_kind_t *rows, int count)
{
    if (form == NULL) {
        return;
    }
    memset(form, 0, sizeof(*form));
    if (rows == NULL || count <= 0) {
        form->row_count = 0;
        return;
    }
    if (count > 3) {
        count = 3;
    }
    form->row_count = count;
    form->current = 0;
    for (int i = 0; i < count; i++) {
        form->kinds[i] = rows[i];
        ft_text_field_init(&form->fields[i], NULL, false, NULL);
    }
}

int ft_form_current(const ft_form_t *form)
{
    return (form != NULL) ? form->current : 0;
}

bool ft_form_on_save_row(const ft_form_t *form)
{
    if (form == NULL || form->row_count == 0) {
        return false;
    }
    return form->kinds[form->current] == FT_FORM_ROW_SAVE;
}

ft_text_field_t *ft_form_current_field(ft_form_t *form)
{
    if (form == NULL || form->row_count == 0) {
        return NULL;
    }
    return &form->fields[form->current];
}

// 把当前编辑框的内容写回 fields[]。
// 切行时调用,保证刚输入的字符不会丢。
static void ft_form_commit(ft_form_t *form)
{
    if (form == NULL || form->row_count == 0) {
        return;
    }
    ft_text_field_t *f = &form->fields[form->current];
    size_t len = ft_text_field_len(f);
    if (len >= sizeof(f->text)) {
        len = sizeof(f->text) - 1;
    }
    // 文本已经在 f->text 里,这里只需要保证结尾 '\0' 与 len 一致。
    f->text[len] = '\0';
}

int ft_form_move(ft_form_t *form, int delta)
{
    if (form == NULL || form->row_count == 0) {
        return 0;
    }
    // 先把当前行的内容落定。
    ft_form_commit(form);

    int next = form->current + (delta > 0 ? 1 : -1);
    if (next < 0) {
        next = form->row_count - 1;
    }
    if (next >= form->row_count) {
        next = 0;
    }
    form->current = next;
    return next;
}

const char *ft_form_value(const ft_form_t *form, int row)
{
    if (form == NULL || row < 0 || row >= form->row_count) {
        return "";
    }
    return form->fields[row].text;
}

bool ft_form_type(ft_form_t *form, char c)
{
    if (form == NULL || form->row_count == 0) {
        return false;
    }
    if (form->kinds[form->current] == FT_FORM_ROW_SAVE) {
        // 保存行不接受字符输入。
        return false;
    }
    ft_text_field_t *f = &form->fields[form->current];
    size_t len = ft_text_field_len(f);
    if (len + 1 >= sizeof(f->text)) {
        return false;
    }
    f->text[len] = c;
    f->text[len + 1] = '\0';
    f->len = len + 1;
    f->cursor = len + 1;
    return true;
}

bool ft_form_backspace(ft_form_t *form)
{
    if (form == NULL || form->row_count == 0) {
        return false;
    }
    if (form->kinds[form->current] == FT_FORM_ROW_SAVE) {
        return false;
    }
    return ft_text_field_backspace_key(&form->fields[form->current]);
}
