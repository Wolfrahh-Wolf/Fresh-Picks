/*
 * delivery.c - Fresh Picks: Post-Order Delivery Management 
 * ==============================================================
 * COMMANDS (argv[1]):
 *
 *   update_status <order_id> <new_status>
 *     → Change the status field of one order in orders.dat.
 *       Valid values: "Order Placed", "Out for Delivery",
 *                     "Delivered", "Cancelled"
 *
 *   cancel_order <order_id>
 *     → Set status to "Cancelled". Only "Order Placed" orders
 *       may be cancelled. ₹50 fee note handled at Flask/UI level.
 *
 *   get_active_orders
 *     → Dump all orders whose status is "Order Placed" or
 *       "Out for Delivery". Used by the delivery dashboard.
 *       Output: SUCCESS|<count> then one row per line.
 *
 *   assign_agent <order_id> <boy_id>
 *     → Override the delivery_boy_id on one specific order.
 *
 *   batch_promote_slot <slot_name>
 *     → Flip all "Order Placed" orders for a given slot to
 *       "Out for Delivery". Returns the count promoted.
 *
 *   list_all_orders
 *     → Dump EVERY order, newest-first, with boy_name + boy_phone
 *       joined from delivery_boys.dat. Used by admin full-view.
 *
 *   list_all_orders_sorted
 *     → Dump ALL orders sorted by slot priority (Morning first),
 *       then timestamp ASC as tiebreaker.
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   SECTION 1: HELPER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/* Walk DeliveryBoyNode SLL and fill out_name/out_phone for boy_id; falls back to "Unknown"/"N/A" */
static void find_boy_in_sll(DeliveryBoyNode* head, const char* boy_id,
                             char* out_name, char* out_phone) {
    strncpy(out_name,  "Unknown", MAX_STR_LEN - 1);
    strncpy(out_phone, "N/A",     MAX_STR_LEN - 1);
    out_name [MAX_STR_LEN - 1] = '\0';
    out_phone[MAX_STR_LEN - 1] = '\0';

    DeliveryBoyNode* curr = head;
    while (curr) {
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

/* Map a delivery slot string to a numeric sort priority: Morning=1, Afternoon=2, Evening=3 */
static int get_slot_priority(const char* slot) {
    if (strcmp(slot, "Morning")   == 0) return 1;
    if (strcmp(slot, "Afternoon") == 0) return 2;
    return 3;
}

/* qsort comparator: slot priority ASC, then timestamp ASC as tiebreaker */
static int compare_orders_priority(const void* a, const void* b) {
    const Order* oa = (const Order*)a;
    const Order* ob = (const Order*)b;
    if (oa->slot_priority != ob->slot_priority)
        return oa->slot_priority - ob->slot_priority;
    return strcmp(oa->timestamp, ob->timestamp);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: COMMAND HANDLER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/* Change the status field of ONE order via O(1) table lookup and persist */
void cmd_update_status(OrderNode** ord_table, int ord_table_size,
                       OrderNode* ord_head,
                       const char* order_id, const char* new_status) {
    const char* VALID[] = {
        "Order Placed", "Out for Delivery", "Delivered", "Cancelled"
    };
    int valid = 0;
    for (int i = 0; i < 4; i++) {
        if (strcmp(new_status, VALID[i]) == 0) { valid = 1; break; }
    }
    if (!valid) { PRINT_ERROR("Invalid status value"); return; }

    if (!ord_head) { PRINT_ERROR("No orders found"); return; }

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

/* Cancel an order; only "Order Placed" orders are eligible */
void cmd_cancel_order(OrderNode** ord_table, int ord_table_size,
                      OrderNode* ord_head,
                      const char* order_id) {
    if (!ord_head) { PRINT_ERROR("No orders found"); return; }

    int idx = get_index_from_id(order_id);
    if (idx < 0 || idx >= ord_table_size || !ord_table[idx]) {
        PRINT_ERROR("Order not found");
        return;
    }

    OrderNode* match = ord_table[idx];
    if (strcmp(match->data.status, "Order Placed") != 0) {
        PRINT_ERROR("Only Order Placed orders can be cancelled");
        return;
    }

    strncpy(match->data.status, "Cancelled", MAX_STR_LEN - 1);
    match->data.status[MAX_STR_LEN - 1] = '\0';

    save_order_sll(ord_head);
    PRINT_SUCCESS("Order cancelled");
}

/* Return all "Order Placed" / "Out for Delivery" orders enriched with boy name+phone */
void cmd_get_active_orders(OrderNode* ord_head, DeliveryBoyNode* boy_head) {
    /* Count active orders first so the header can be printed */
    int        active = 0;
    OrderNode* curr   = ord_head;
    while (curr) {
        if (strcmp(curr->data.status, "Order Placed")     == 0 ||
            strcmp(curr->data.status, "Out for Delivery") == 0)
            active++;
        curr = curr->next;
    }

    printf("SUCCESS|%d\n", active);

    curr = ord_head;
    while (curr) {
        if (strcmp(curr->data.status, "Order Placed")     != 0 &&
            strcmp(curr->data.status, "Out for Delivery") != 0) {
            curr = curr->next;
            continue;
        }

        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, curr->data.delivery_boy_id, boy_name, boy_phone);

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
        curr = curr->next;
    }
}

/* Override the delivery_boy_id on one specific order via O(1) lookup and persist */
void cmd_assign_agent(OrderNode** ord_table, int ord_table_size,
                      OrderNode* ord_head,
                      const char* order_id, const char* boy_id) {
    if (!ord_head) { PRINT_ERROR("No orders found"); return; }

    int idx = get_index_from_id(order_id);
    if (idx < 0 || idx >= ord_table_size || !ord_table[idx]) {
        PRINT_ERROR("Order not found");
        return;
    }

    OrderNode* match = ord_table[idx];
    strncpy(match->data.delivery_boy_id, boy_id, MAX_ID_LEN - 1);
    match->data.delivery_boy_id[MAX_ID_LEN - 1] = '\0';

    save_order_sll(ord_head);
    PRINT_SUCCESS("Agent assigned");
}

/* Flip all "Order Placed" orders for a given slot to "Out for Delivery"; returns count promoted */
void cmd_batch_promote_slot(OrderNode* ord_head, const char* slot_name) {
    if (strcmp(slot_name, "Morning")   != 0 &&
        strcmp(slot_name, "Afternoon") != 0 &&
        strcmp(slot_name, "Evening")   != 0) {
        PRINT_ERROR("Invalid slot name");
        return;
    }

    if (!ord_head) { PRINT_SUCCESS("0"); return; }

    int        promoted = 0;
    OrderNode* curr     = ord_head;
    while (curr) {
        if (strcmp(curr->data.delivery_slot, slot_name) == 0 &&
            strcmp(curr->data.status, "Order Placed")   == 0) {
            strncpy(curr->data.status, "Out for Delivery", MAX_STR_LEN - 1);
            curr->data.status[MAX_STR_LEN - 1] = '\0';
            promoted++;
        }
        curr = curr->next;
    }

    save_order_sll(ord_head);

    char result[32];
    snprintf(result, sizeof(result), "%d", promoted);
    PRINT_SUCCESS(result);
}

/* Dump every order newest-first enriched with boy_name+boy_phone; used by admin full-view */
void cmd_list_all_orders(OrderNode* ord_head, DeliveryBoyNode* boy_head) {
    int total = sll_count_orders(ord_head);
    printf("SUCCESS|%d\n", total);

    if (total == 0) return;

    /* Collect node pointers for reverse (newest-first) iteration */
    OrderNode** ptrs = (OrderNode**)malloc(sizeof(OrderNode*) * total);
    if (!ptrs) return;

    int        idx  = 0;
    OrderNode* curr = ord_head;
    while (curr) { ptrs[idx++] = curr; curr = curr->next; }

    for (int i = idx - 1; i >= 0; i--) {
        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, ptrs[i]->data.delivery_boy_id, boy_name, boy_phone);

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

/* Dump all orders sorted by slot priority ASC then timestamp ASC, enriched with boy info */
void cmd_list_all_orders_sorted(OrderNode* ord_head, DeliveryBoyNode* boy_head) {
    int total = sll_count_orders(ord_head);
    printf("SUCCESS|%d\n", total);

    if (total == 0) return;

    /* Copy SLL data into a flat array for qsort */
    Order* arr = (Order*)malloc(sizeof(Order) * total);
    if (!arr) return;

    int        idx  = 0;
    OrderNode* curr = ord_head;
    while (curr) {
        arr[idx]               = curr->data;
        arr[idx].slot_priority = get_slot_priority(curr->data.delivery_slot);
        idx++;
        curr = curr->next;
    }

    qsort(arr, total, sizeof(Order), compare_orders_priority);

    for (int i = 0; i < total; i++) {
        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, arr[i].delivery_boy_id, boy_name, boy_phone);

        printf("%s|%s|%.2f|%s|%s|%s|%s|%s|%s|%s\n",
            arr[i].order_id,
            arr[i].user_id,
            arr[i].total_amount,
            arr[i].delivery_slot,
            arr[i].delivery_boy_id,
            arr[i].status,
            arr[i].timestamp,
            arr[i].items_string,
            boy_name,
            boy_phone
        );
    }

    free(arr);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 3: MAIN — Command Dispatcher
   ═════════════════════════════════════════════════════════════ */

/* Entry point: loads all SLLs + builds pointer tables once, dispatches to handlers, then frees */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command. Usage: ./delivery <command> [args]");
        return 1;
    }

    /* Load SLLs once for the lifetime of this process */
    OrderNode*       ord_head = load_order_sll();
    DeliveryBoyNode* boy_head = load_delivery_boy_sll();

    /* Build order pointer table; delivery boys have no build_*_table, use SLL traversal */
    int          ord_table_size = 0;
    OrderNode**  ord_table      = build_order_table(ord_head, &ord_table_size);
    if (!ord_table && ord_head) {
        PRINT_ERROR("Memory allocation failed");
        free_order_sll(ord_head);
        free_delivery_boy_sll(boy_head);
        return 1;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "update_status") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: update_status <order_id> <new_status>"); }
        else cmd_update_status(ord_table, ord_table_size, ord_head, argv[2], argv[3]);

    } else if (strcmp(cmd, "cancel_order") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: cancel_order <order_id>"); }
        else cmd_cancel_order(ord_table, ord_table_size, ord_head, argv[2]);

    } else if (strcmp(cmd, "get_active_orders") == 0) {
        cmd_get_active_orders(ord_head, boy_head);

    } else if (strcmp(cmd, "assign_agent") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: assign_agent <order_id> <boy_id>"); }
        else cmd_assign_agent(ord_table, ord_table_size, ord_head, argv[2], argv[3]);

    } else if (strcmp(cmd, "batch_promote_slot") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: batch_promote_slot <slot_name>"); }
        else cmd_batch_promote_slot(ord_head, argv[2]);

    } else if (strcmp(cmd, "list_all_orders") == 0) {
        cmd_list_all_orders(ord_head, boy_head);

    } else if (strcmp(cmd, "list_all_orders_sorted") == 0) {
        cmd_list_all_orders_sorted(ord_head, boy_head);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
    }

    /* Free all tables and SLLs */
    free(ord_table);
    free_order_sll(ord_head);
    free_delivery_boy_sll(boy_head);

    return 0;
}
