#include <stddef.h>
#include <stdint.h>

#include "util.h"
#include "page.h"


static Page *s_head_page = NULL;
static Page *s_active_page = NULL;

void page_register(Page *page)
{
    page->next = s_head_page;
    s_head_page = page;
}

Page *page_find(const char *name)
{
    Page *page = s_head_page;
    while(page)
    {
        if (!strcmp(name, page->name))
        {
            return page;
        }
        page = page->next;
    }
    return NULL;
}

Page *page_get_active()
{
    return s_active_page;
}

void page_set_active(Page *page)
{
    if (s_active_page)
    {
        if (s_active_page->deinit)
        {
            s_active_page->deinit();
        }
    }

    s_active_page = page;

    if (s_active_page)
    {
        if (s_active_page->init)
        {
            s_active_page->init();
        }
    }
}
    
void page_set_next_active()
{
    if (s_active_page && s_active_page->next)
    {
        page_set_active(s_active_page->next);
    }
    else
    {
        page_set_active(s_head_page);
    }
}

void page_update()
{
    if (s_active_page && s_active_page->update)
    {
        s_active_page->update();
    }
}

