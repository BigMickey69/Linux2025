#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>


struct list_head {
    struct list_head *next, *prev;
};

#define INIT_LIST_HEAD(ptr)          \
    do {                             \
        (ptr)->next = (ptr);         \
        (ptr)->prev = (ptr);         \
    } while (0)

#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define list_empty(ptr) ((ptr)->next == (ptr))

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
    new->prev = head->prev;
    new->next = head;
    head->prev->next = new;
    head->prev = new;
}

static inline void list_del(struct list_head *entry)
{
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    INIT_LIST_HEAD(entry);
}

static inline void list_move_tail(struct list_head *node,
    struct list_head *head)
{
    list_del(node);
    list_add_tail(node, head);
}

static inline void list_splice_tail(struct list_head *list,
    struct list_head *head)
{
    struct list_head *head_last = head->prev;
    struct list_head *list_first = list->next;
    struct list_head *list_last = list->prev;

    if (list_empty(list))
        return;

    head->prev = list_last;
    list_last->next = head;

    list_first->prev = head_last;
    head_last->next = list_first;
}

static inline void list_splice_tail_init(struct list_head *list,
    struct list_head *head)
{
    list_splice_tail(list, head);
    INIT_LIST_HEAD(list);
}

static inline void list_splice_init(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list)) {
        struct list_head *first = list->next;
        struct list_head *last  = list->prev;
        struct list_head *at    = head->next;
        first->prev = head;
        head->next = first;
        last->next = at;
        at->prev = last;
        INIT_LIST_HEAD(list);
    }
}


typedef struct element {
    char value[10];
    struct list_head list;
} element_t;

typedef struct queue_contex {
    struct list_head chain;
    struct list_head q_data;
    struct list_head *q;
    int size;
} queue_contex_t;



int compare(struct list_head *a, struct list_head *b)
{
    element_t *ea = list_entry(a, element_t, list);
    element_t *eb = list_entry(b, element_t, list);
    return (strcmp(ea->value, eb->value) > 0);
}

element_t *q_remove_head(struct list_head *q, char *sp, int size)
{
    if (list_empty(q))
        return NULL;
    struct list_head *node = q->next;
    list_del(node);
    element_t *elem = list_entry(node, element_t, list);
    if (sp && size > 0) {
        strncpy(sp, elem->value, size - 1);
        sp[size - 1] = '\0';
    }
    return elem;
}


void merge_lists(struct list_head *h1, struct list_head *h2, bool descend)
{
    struct list_head merged;
    INIT_LIST_HEAD(&merged);

    while (!list_empty(h1) && !list_empty(h2)) {
        element_t *e1 = list_entry(h1->next, element_t, list);
        element_t *e2 = list_entry(h2->next, element_t, list);
        if (descend ? strcmp(e1->value, e2->value) >= 0
                    : strcmp(e1->value, e2->value) <= 0) {
            list_move_tail(h1->next, &merged);
        } else {
            list_move_tail(h2->next, &merged);
        }
    }

    if (!list_empty(h1)) {
        list_splice_tail_init(h1, &merged);
    }

    if (!list_empty(h2)) {
        list_splice_tail_init(h2, &merged);
    }

    list_splice_init(&merged, h1);
    INIT_LIST_HEAD(h2);
}




int q_merge(struct list_head *head, bool descend)
{
    if (list_empty(head))
        return 0;

    queue_contex_t *first = list_entry(head->next, queue_contex_t, chain);

    for (struct list_head *cur = head->next->next; cur != head;
         cur = cur->next) {

        queue_contex_t *second = list_entry(cur, queue_contex_t, chain);
        first->size += second->size;
        second->size = 0;
        merge_lists(first->q, second->q, descend);
    }
    return first->size;
}


element_t *create_element(const char *val)
{
    element_t *elem = malloc(sizeof(element_t));
    if (!elem) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    strncpy(elem->value, val, sizeof(elem->value) - 1);
    elem->value[sizeof(elem->value) - 1] = '\0';
    INIT_LIST_HEAD(&elem->list);
    return elem;
}



void q_add_tail(struct list_head *q, element_t *elem, queue_contex_t *qc)
{
    list_add_tail(&elem->list, q);
    qc->size++;
}


void print_queue(struct list_head *q)
{
    struct list_head *pos;
    list_for_each(pos, q) {
        element_t *elem = list_entry(pos, element_t, list);
        printf("%s ", elem->value);
    }
    printf("\n");
}


queue_contex_t *create_queue_contex(void)
{
    queue_contex_t *qc = malloc(sizeof(queue_contex_t));
    if (!qc) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    INIT_LIST_HEAD(&qc->chain);
    INIT_LIST_HEAD(&qc->q_data);
    qc->q = &qc->q_data;
    qc->size = 0;
    return qc;
}


int main(void)
{
    struct list_head chain_head;
    INIT_LIST_HEAD(&chain_head);


    queue_contex_t *qc1 = create_queue_contex();
    queue_contex_t *qc2 = create_queue_contex();
    queue_contex_t *qc3 = create_queue_contex();


    list_add_tail(&qc1->chain, &chain_head);
    list_add_tail(&qc2->chain, &chain_head);
    list_add_tail(&qc3->chain, &chain_head);


    q_add_tail(qc1->q, create_element("apple"), qc1);
    q_add_tail(qc1->q, create_element("banana"), qc1);
    q_add_tail(qc1->q, create_element("cherry"), qc1);


    q_add_tail(qc2->q, create_element("apricot"), qc2);
    q_add_tail(qc2->q, create_element("blueberry"), qc2);
    q_add_tail(qc2->q, create_element("citrus"), qc2);


    q_add_tail(qc3->q, create_element("avocado"), qc3);
    q_add_tail(qc3->q, create_element("blackberry"), qc3);
    q_add_tail(qc3->q, create_element("cranberry"), qc3);


    printf("Before merge:\n");
    printf("Queue 1: "); print_queue(qc1->q);
    printf("Queue 2: "); print_queue(qc2->q);
    printf("Queue 3: "); print_queue(qc3->q);


    int merged_size = q_merge(&chain_head, false);
    printf("\nAfter merge (merged size = %d):\n", merged_size);
    print_queue(qc1->q);

    return 0;
}
