/*
 * order.c - Fresh Picks: Shopping Cart, Payment & Order Management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "models.h"

/* ═════════════════════════════════════════════════════════════
   SECTION 1: HELPER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/* Map a delivery slot name to a numeric priority; lower = more urgent. */
int get_slot_priority(const char* slot) {
    if (strcmp(slot, "Morning")   == 0) return 1;
    if (strcmp(slot, "Afternoon") == 0) return 2;
    return 3;
}

/* Fill out_buf with the current local time as "YYYY-MM-DD HH:MM:SS". */
void get_current_timestamp(char* out_buf) {
    time_t     now = time(NULL);
    struct tm* t   = localtime(&now);
    strftime(out_buf, TIMESTAMP_LEN, "%Y-%m-%d %H:%M:%S", t);
}

/* Build the path carts/<user_id>_cart.txt into out_path. */
void get_cart_filename(const char* user_id, char* out_path) {
    snprintf(out_path, MAX_LINE_LEN, "%s%s_cart.txt", CART_DIR, user_id);
}

/* Linear search through DeliveryBoyNode SLL; fills out_name / out_phone. */
void find_boy_in_sll(DeliveryBoyNode* sll_head, const char* boy_id,
                     char* out_name, char* out_phone) {
    strncpy(out_name,  "Unknown", MAX_STR_LEN - 1);
    strncpy(out_phone, "N/A",     MAX_STR_LEN - 1);
    out_name [MAX_STR_LEN - 1] = '\0';
    out_phone[MAX_STR_LEN - 1] = '\0';

    DeliveryBoyNode* curr = sll_head;
    while (curr != NULL) {
        if (strcmp(curr->data.boy_id, boy_id) == 0) {
            strncpy(out_name,  curr->data.name,  MAX_STR_LEN - 1);
            strncpy(out_phone, curr->data.phone, MAX_STR_LEN - 1);
            out_name [MAX_STR_LEN - 1] = '\0';
            out_phone[MAX_STR_LEN - 1] = '\0';
            return;
        }
        curr = curr->next;
    }
}

/* Read carts/<user_id>_cart.txt and build a DLL; returns NULL if absent. */
CartNode* load_cart_from_file(const char* user_id) {
    char path[MAX_LINE_LEN];
    get_cart_filename(user_id, path);

    FILE* fp = fopen(path, "r");
    if (!fp) return NULL;

    CartNode* head = NULL;
    char      line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        char  veg_id[MAX_ID_LEN], name[MAX_STR_LEN];
        int   qty_g, is_free;
        float price;

        char* tok = strtok(line, "|");
        if (!tok) continue;
        strncpy(veg_id, tok, MAX_ID_LEN  - 1);  tok = strtok(NULL, "|");
        if (!tok) continue;
        strncpy(name,   tok, MAX_STR_LEN - 1);  tok = strtok(NULL, "|");
        if (!tok) continue;
        qty_g   = atoi(tok);                      tok = strtok(NULL, "|");
        if (!tok) continue;
        price   = atof(tok);                      tok = strtok(NULL, "|");
        is_free = tok ? atoi(tok) : 0;

        veg_id[MAX_ID_LEN  - 1] = '\0';
        name  [MAX_STR_LEN - 1] = '\0';

        CartNode* node = dll_create_node(veg_id, name, qty_g, price, is_free);
        dll_append(&head, node);
    }
    fclose(fp);
    return head;
}

/* Walk the DLL and overwrite carts/<user_id>_cart.txt (Rule 10 exception). */
void save_cart_to_file(const char* user_id, CartNode* head) {
    char path[MAX_LINE_LEN];
    get_cart_filename(user_id, path);

    FILE* fp = fopen(path, "w");
    if (!fp) return;

    CartNode* curr = head;
    while (curr != NULL) {
        fprintf(fp, "%s|%s|%d|%.2f|%d\n",
            curr->veg_id,
            curr->name,
            curr->qty_g,
            curr->price_per_1000g,
            curr->is_free
        );
        curr = curr->next;
    }
    fclose(fp);
}

/*
 * If cart_total >= Rs.500, append free items to the cart DLL using the
 * already-loaded free_item_head SLL and free_item_table for O(1) stock access.
 * multiplier = (int)(cart_total / 500); final_free_qty_g = multiplier * free_qty_g
 * Caller (main) owns free_item_head lifetime; saves SLL only when modified.
 */
void check_and_apply_freebies(CartNode**     head,
                               float          cart_total,
                               FreeItemNode*  free_item_head,
                               FreeItemNode** free_item_table,
                               int            free_item_table_size) {
    if (cart_total < 500.0f) return;
    if (!free_item_head)            return;

    /* free_item_table / free_item_table_size reserved for future O(1) stock lookups;
       current dataset is small so we traverse free_item_head directly. */
    (void)free_item_table;
    (void)free_item_table_size;

    int multiplier = (int)(cart_total / 500.0f);
    int modified   = 0;

    FreeItemNode* curr = free_item_head;
    while (curr != NULL) {
        if (cart_total < curr->data.min_trigger_amt) {
            curr = curr->next;
            continue;
        }

        int qty_to_give = multiplier * curr->data.free_qty_g;
        if (curr->data.stock_g < qty_to_give) {
            curr = curr->next;
            continue;
        }

        dll_update_or_append(head,
            curr->data.vf_id,
            curr->data.name,
            qty_to_give,
            0.0f,
            1
        );

        curr->data.stock_g -= qty_to_give;
        modified = 1;
        curr = curr->next;
    }

    if (modified)
        save_free_item_sll(free_item_head);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: COMMAND HANDLER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/*
 * Add or update one vegetable in the user's cart DLL.
 * Uses O(1) veg_table lookup instead of linear SLL walk.
 * OUTPUT:  SUCCESS|Item added to cart
 *          ERROR|reason
 */
void cmd_add_to_cart(const char*  user_id,
                     const char*  veg_id,
                     int          qty_g,
                     VegNode**    veg_table,
                     int          veg_table_size) {
    if (qty_g <= 0) {
        PRINT_ERROR("Quantity must be positive");
        return;
    }
    if (qty_g % 50 != 0) {
        PRINT_ERROR("Quantity must be a multiple of 50g");
        return;
    }
    if (!veg_table) {
        PRINT_ERROR("No products available");
        return;
    }

    /* O(1) lookup via pointer table */
    int idx = get_index_from_id(veg_id);
    if (idx < 0 || idx >= veg_table_size || !veg_table[idx]) {
        PRINT_ERROR("Vegetable not found");
        return;
    }
    VegNode* match = veg_table[idx];

    if (qty_g > match->data.stock_g) {
        PRINT_ERROR("Insufficient stock");
        return;
    }

    char  v_name[MAX_STR_LEN];
    float v_price = match->data.price_per_1000g;
    strncpy(v_name, match->data.name, MAX_STR_LEN - 1);
    v_name[MAX_STR_LEN - 1] = '\0';

    CartNode* head = load_cart_from_file(user_id);
    dll_update_or_append(&head, veg_id, v_name, qty_g, v_price, 0);
    save_cart_to_file(user_id, head);
    dll_free_all(head);

    PRINT_SUCCESS("Item added to cart");
}

/*
 * Load the user's cart DLL and print all items plus grand total.
 * OUTPUT:  SUCCESS|<grand_total>
 *          veg_id|name|qty_g|price_per_1000g|item_total|is_free
 */
void cmd_view_cart(const char* user_id) {
    CartNode* head  = load_cart_from_file(user_id);
    float     total = dll_get_total(head);

    printf("SUCCESS|%.2f\n", total);

    CartNode* curr = head;
    while (curr != NULL) {
        printf("%s|%s|%d|%.2f|%.2f|%d\n",
            curr->veg_id,
            curr->name,
            curr->qty_g,
            curr->price_per_1000g,
            curr->item_total,
            curr->is_free
        );
        curr = curr->next;
    }
    dll_free_all(head);
}

/*
 * Remove one item from the cart DLL by veg_id, then persist.
 * OUTPUT:  SUCCESS|Item removed from cart
 */
void cmd_remove_item(const char* user_id, const char* veg_id) {
    CartNode* head = load_cart_from_file(user_id);
    dll_remove(&head, veg_id);
    save_cart_to_file(user_id, head);
    dll_free_all(head);
    PRINT_SUCCESS("Item removed from cart");
}

/*
 * Full payment pipeline — validates cart, applies freebies, deducts stock,
 * assigns a delivery boy, records the order.
 * Receives pre-loaded tables from main(); generates order ID from ord_count.
 * OUTPUT:  SUCCESS|order_id|total|slot|boy_name|boy_phone|items_string
 *          ERROR|reason
 */
void cmd_checkout(const char*      user_id,
                  const char*      slot,
                  VegNode*         veg_head,
                  VegNode**        veg_table,
                  int              veg_table_size,
                  OrderNode*       ord_head,
                  int              ord_count,
                  FreeItemNode*    free_item_head,
                  FreeItemNode**   free_item_table,
                  int              free_item_table_size,
                  DeliveryBoyNode* boy_sll) {

    /* ── Step 1: Load cart ─────────────────────────────────────── */
    CartNode* head = load_cart_from_file(user_id);
    if (!head) {
        PRINT_ERROR("Cart is empty");
        return;
    }

    /* ── Step 2: Minimum order check ──────────────────────────── */
    float total = dll_get_total(head);
    if (total < 100.0f) {
        dll_free_all(head);
        PRINT_ERROR("Minimum order is Rs.100");
        return;
    }

    /* ── Step 3: Stock recheck via veg table (O(1) per item) ───── */
    if (!veg_table) {
        dll_free_all(head);
        PRINT_ERROR("Product data unavailable");
        return;
    }

    CartNode* curr = head;
    while (curr != NULL) {
        if (curr->is_free) { curr = curr->next; continue; }

        int idx = get_index_from_id(curr->veg_id);
        if (idx < 0 || idx >= veg_table_size || !veg_table[idx]) {
            dll_free_all(head);
            PRINT_ERROR("Product no longer available");
            return;
        }
        VegNode* vmatch = veg_table[idx];
        if (vmatch->data.stock_g < curr->qty_g) {
            char err[MAX_STR_LEN + 50];
            snprintf(err, sizeof(err),
                "Insufficient stock for %s (available: %dg)",
                vmatch->data.name, vmatch->data.stock_g);
            dll_free_all(head);
            PRINT_ERROR(err);
            return;
        }
        curr = curr->next;
    }

    /* ── Step 4: Apply freebies (multiplier logic) ─────────────── */
    check_and_apply_freebies(&head, total, free_item_head, free_item_table, free_item_table_size);
    total = dll_get_total(head);

    /* ── Step 5: Deduct stock for all paid items (O(1) per item) ── */
    curr = head;
    while (curr != NULL) {
        if (!curr->is_free) {
            int idx = get_index_from_id(curr->veg_id);
            if (idx >= 0 && idx < veg_table_size && veg_table[idx])
                veg_table[idx]->data.stock_g -= curr->qty_g;
        }
        curr = curr->next;
    }
    save_veg_sll(veg_head);

    /* ── Step 6: Delivery boy assignment (CLL round-robin) ─────── */
    DeliveryNode* cll = cll_build_from_sll(boy_sll);

    DeliveryBoy assigned_boy;
    char boy_id   [MAX_ID_LEN]  = "NONE";
    char boy_name [MAX_STR_LEN] = "Unassigned";
    char boy_phone[MAX_STR_LEN] = "N/A";

    if (cll && cll_assign_delivery(cll, &assigned_boy, boy_sll)) {
        strncpy(boy_id,    assigned_boy.boy_id, MAX_ID_LEN  - 1);
        strncpy(boy_name,  assigned_boy.name,   MAX_STR_LEN - 1);
        strncpy(boy_phone, assigned_boy.phone,  MAX_STR_LEN - 1);
        boy_id   [MAX_ID_LEN  - 1] = '\0';
        boy_name [MAX_STR_LEN - 1] = '\0';
        boy_phone[MAX_STR_LEN - 1] = '\0';
        /* NOTE: save_delivery_boy_sll is called inside cll_assign_delivery(). */
    }
    cll_free(cll);

    /* ── Step 7a: Capture timestamp ────────────────────────────── */
    char timestamp[TIMESTAMP_LEN];
    get_current_timestamp(timestamp);

    /* ── Step 7b: Generate order ID from pre-computed count ─────── */
    char order_id[MAX_ID_LEN];
    snprintf(order_id, MAX_ID_LEN, "ORD%d", 1001 + ord_count);

    /* ── Step 7c: Build items_string with price snapshot ────────── */
    char items_string[MAX_LINE_LEN] = "";
    curr = head;
    while (curr != NULL) {
        char safe_name[MAX_STR_LEN];
        strncpy(safe_name, curr->name, MAX_STR_LEN - 1);
        safe_name[MAX_STR_LEN - 1] = '\0';
        for (char* p = safe_name; *p; p++) {
            if (*p == ':' || *p == ',') *p = ' ';
        }

        char part[256];
        snprintf(part, sizeof(part), "%s:%s:%d:%.2f",
            curr->veg_id,
            safe_name,
            curr->qty_g,
            curr->price_per_1000g
        );
        if (strlen(items_string) > 0)
            strncat(items_string, ",", MAX_LINE_LEN - strlen(items_string) - 1);
        strncat(items_string, part, MAX_LINE_LEN - strlen(items_string) - 1);
        curr = curr->next;
    }

    /* ── Step 7d: Build Order struct ───────────────────────────── */
    Order o;
    memset(&o, 0, sizeof(Order));
    strncpy(o.order_id,        order_id,       MAX_ID_LEN   - 1);
    strncpy(o.user_id,         user_id,        MAX_ID_LEN   - 1);
    o.total_amount = total;
    strncpy(o.delivery_slot,   slot,           MAX_STR_LEN  - 1);
    strncpy(o.delivery_boy_id, boy_id,         MAX_ID_LEN   - 1);
    strncpy(o.status,          "Order Placed", MAX_STR_LEN  - 1);
    strncpy(o.timestamp,       timestamp,      TIMESTAMP_LEN - 1);
    strncpy(o.items_string,    items_string,   MAX_LINE_LEN  - 1);
    o.slot_priority = get_slot_priority(slot);

    /* ── Step 8: Append to the live ord_head SLL and save ──────── */
    OrderNode* new_node = (OrderNode*)malloc(sizeof(OrderNode));
    if (!new_node) {
        dll_free_all(head);
        PRINT_ERROR("Memory allocation failed");
        return;
    }
    new_node->data = o;
    new_node->next = NULL;

    if (!ord_head) {
        /* Edge case: first-ever order — caller's pointer stays NULL;
           we append locally and save so the file is written correctly. */
        ord_head = new_node;
    } else {
        OrderNode* tail = ord_head;
        while (tail->next) tail = tail->next;
        tail->next = new_node;
    }
    save_order_sll(ord_head);

    /* ── Step 9: Delete the cart file ──────────────────────────── */
    char cart_path[MAX_LINE_LEN];
    get_cart_filename(user_id, cart_path);
    remove(cart_path);

    /* ── Step 10: Print confirmation for Flask ─────────────────── */
    printf("SUCCESS|%s|%.2f|%s|%s|%s|%s\n",
        order_id,
        total,
        slot,
        boy_name,
        boy_phone,
        items_string
    );

    dll_free_all(head);
}

/*
 * Print all orders belonging to user_id, enriched with delivery boy details.
 * Uses O(1) order_table lookup to skip non-matching entries efficiently.
 * OUTPUT:  SUCCESS|
 *          order_id|user_id|total|slot|boy_id|status|timestamp|items_string|boy_name|boy_phone
 */
void cmd_get_orders(const char*      user_id,
                    OrderNode*       ord_head,
                    DeliveryBoyNode* boy_head) {
    printf("SUCCESS|\n");

    /* Traverse SLL — user_id is not the primary index key, so we walk. */
    OrderNode* curr = ord_head;
    while (curr != NULL) {
        if (strcmp(curr->data.user_id, user_id) == 0) {
            char boy_name [MAX_STR_LEN] = "Unknown";
            char boy_phone[MAX_STR_LEN] = "N/A";
            find_boy_in_sll(boy_head, curr->data.delivery_boy_id,
                            boy_name, boy_phone);

            printf("%s|%s|%.2f|%s|%s|%s|%s|%s|%s|%s\n",
                curr->data.order_id,
                curr->data.user_id,
                curr->data.total_amount,
                curr->data.delivery_slot,
                curr->data.delivery_boy_id,
                curr->data.status,
                curr->data.timestamp,
                curr->data.items_string,
                boy_name,
                boy_phone
            );
        }
        curr = curr->next;
    }
}

/*
 * Change the status of one specific order using O(1) order_table lookup.
 * OUTPUT:  SUCCESS|Status updated
 *          ERROR|Order not found
 */
void cmd_update_order_status(const char*  order_id,
                             const char*  new_status,
                             OrderNode*   ord_head,
                             OrderNode**  ord_table,
                             int          ord_table_size) {
    if (!ord_table) {
        PRINT_ERROR("No orders found");
        return;
    }

    /* O(1) direct index into the order pointer table */
    int idx = get_index_from_id(order_id);
    if (idx < 0 || idx >= ord_table_size || !ord_table[idx]) {
        PRINT_ERROR("Order not found");
        return;
    }

    OrderNode* match = ord_table[idx];
    strncpy(match->data.status, new_status, MAX_STR_LEN - 1);
    match->data.status[MAX_STR_LEN - 1] = '\0';

    save_order_sll(ord_head);
    PRINT_SUCCESS("Status updated");
}

/*
 * Dump every order newest-first, enriched with delivery boy name and phone.
 * OUTPUT:  SUCCESS|<total_count>
 *          order_id|user_id|total|slot|boy_id|status|timestamp|items_string|boy_name|boy_phone
 */
void cmd_list_all_orders(OrderNode*       ord_head,
                         int              total_count,
                         DeliveryBoyNode* boy_head) {
    printf("SUCCESS|%d\n", total_count);

    if (total_count == 0) return;

    /* Collect node pointers to iterate in reverse (newest-first). */
    OrderNode** ptrs = (OrderNode**)malloc(sizeof(OrderNode*) * total_count);
    if (!ptrs) return;

    int        idx  = 0;
    OrderNode* curr = ord_head;
    while (curr != NULL) {
        ptrs[idx++] = curr;
        curr = curr->next;
    }

    for (int i = idx - 1; i >= 0; i--) {
        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, ptrs[i]->data.delivery_boy_id,
                        boy_name, boy_phone);

        printf("%s|%s|%.2f|%s|%s|%s|%s|%s|%s|%s\n",
            ptrs[i]->data.order_id,
            ptrs[i]->data.user_id,
            ptrs[i]->data.total_amount,
            ptrs[i]->data.delivery_slot,
            ptrs[i]->data.delivery_boy_id,
            ptrs[i]->data.status,
            ptrs[i]->data.timestamp,
            ptrs[i]->data.items_string,
            boy_name,
            boy_phone
        );
    }

    free(ptrs);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 3: MAIN — Load, Build, Dispatch, Free
   ═════════════════════════════════════════════════════════════ */

/*
 * Entry point. Loads all SLLs, builds Pointer Tables, dispatches to the
 * matching handler, then frees everything before exit.
 * Flask calls this binary via:
 *   subprocess.run(["./order", "checkout", "U1001", "Morning"], ...)
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command provided. Usage: ./order <command> [args]");
        return 1;
    }

    const char* cmd = argv[1];

    /* ── Load all SLLs ─────────────────────────────────────────── */
    VegNode*         veg_head = load_veg_sll();
    OrderNode*       ord_head = load_order_sll();
    FreeItemNode*    free_item_head  = load_free_item_sll();
    DeliveryBoyNode* boy_head = load_delivery_boy_sll();

    /* ── Build Pointer Tables ──────────────────────────────────── */
    int veg_table_size = 0;
    VegNode** veg_table = build_veg_table(veg_head, &veg_table_size);

    int ord_table_size = 0;
    OrderNode** ord_table = build_order_table(ord_head, &ord_table_size);

    int free_item_table_size = 0;
    FreeItemNode** free_item_table = build_free_table(free_item_head, &free_item_table_size);

    /* ── Pre-compute order count for checkout ID generation ─────── */
    int ord_count = sll_count_orders(ord_head);

    /* ── Dispatch ──────────────────────────────────────────────── */
    if (strcmp(cmd, "add_to_cart") == 0) {
        if (argc < 5) { PRINT_ERROR("Usage: add_to_cart <user_id> <veg_id> <grams>"); goto cleanup; }
        cmd_add_to_cart(argv[2], argv[3], atoi(argv[4]),
                        veg_table, veg_table_size);

    } else if (strcmp(cmd, "view_cart") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: view_cart <user_id>"); goto cleanup; }
        cmd_view_cart(argv[2]);

    } else if (strcmp(cmd, "remove_item") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: remove_item <user_id> <veg_id>"); goto cleanup; }
        cmd_remove_item(argv[2], argv[3]);

    } else if (strcmp(cmd, "checkout") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: checkout <user_id> <slot>"); goto cleanup; }
        cmd_checkout(argv[2], argv[3],
                     veg_head,  veg_table, veg_table_size,
                     ord_head,  ord_count,
                     free_item_head,   free_item_table,  free_item_table_size,
                     boy_head);

    } else if (strcmp(cmd, "get_orders") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: get_orders <user_id>"); goto cleanup; }
        cmd_get_orders(argv[2], ord_head, boy_head);

    } else if (strcmp(cmd, "update_order_status") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: update_order_status <order_id> <status>"); goto cleanup; }
        cmd_update_order_status(argv[2], argv[3],
                                ord_head, ord_table, ord_table_size);

    } else if (strcmp(cmd, "list_all_orders") == 0) {
        cmd_list_all_orders(ord_head, ord_count, boy_head);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
    }

cleanup:
    /* ── Free all Pointer Tables ───────────────────────────────── */
    free(veg_table);
    free(ord_table);
    free(free_item_table);

    /* ── Free all SLLs ─────────────────────────────────────────── */
    free_veg_sll(veg_head);
    free_order_sll(ord_head);
    free_free_item_sll(free_item_head);
    free_delivery_boy_sll(boy_head);

    return 0;
}
