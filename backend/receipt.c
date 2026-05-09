/*
 * receipt.c - Fresh Picks: Order Receipt Data Extractor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   HELPER FUNCTION
   ═════════════════════════════════════════════════════════════ */

/* Walk the DeliveryBoyNode SLL for boy_id; writes into out_name/out_phone,
   falling back to "Unknown"/"N/A" if not found. Linear traversal is
   intentional — no build_delivery_boy_table exists in utils.c. */
void find_delivery_boy(DeliveryBoyNode* head, const char* boy_id,
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


/* ═════════════════════════════════════════════════════════════
   MAIN
   ═════════════════════════════════════════════════════════════ */

/* Entry point: loads SLLs, builds pointer tables, performs lookups,
   prints the unified receipt line, then frees all resources. */
int main(int argc, char* argv[]) {

    if (argc < 2) {
        PRINT_ERROR("Usage: ./receipt <order_id>");
        return 1;
    }

    const char* order_id = argv[1];

    /* ── Load SLLs ───────────────────────────────────────────────── */
    OrderNode* ord_head = load_order_sll();
    if (!ord_head) {
        PRINT_ERROR("No orders found");
        return 1;
    }

    UserNode* usr_head = load_user_sll();
    if (!usr_head) {
        free_order_sll(ord_head);
        PRINT_ERROR("No users found");
        return 1;
    }

    DeliveryBoyNode* boy_head = load_delivery_boy_sll();
    /* boy_head may be NULL if no delivery boys are on file; find_delivery_boy
       handles that gracefully by writing its fallback values immediately. */

    /* ── Build pointer tables ────────────────────────────────────── */
    int ord_max = 0;
    OrderNode** ord_table = build_order_table(ord_head, &ord_max);
    if (!ord_table) {
        free_delivery_boy_sll(boy_head);
        free_user_sll(usr_head);
        free_order_sll(ord_head);
        PRINT_ERROR("Memory allocation failed");
        return 1;
    }

    int usr_max = 0;
    UserNode** usr_table = build_user_table(usr_head, &usr_max);
    if (!usr_table) {
        free(ord_table);
        free_delivery_boy_sll(boy_head);
        free_user_sll(usr_head);
        free_order_sll(ord_head);
        PRINT_ERROR("Memory allocation failed");
        return 1;
    }

    /* ── Step 1: O(1) order lookup via pointer table ─────────────── */
    int ord_idx = get_index_from_id(order_id);
    if (ord_idx < 0 || ord_idx >= ord_max || !ord_table[ord_idx]) {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Order not found: %s", order_id);
        free(usr_table);
        free(ord_table);
        free_delivery_boy_sll(boy_head);
        free_user_sll(usr_head);
        free_order_sll(ord_head);
        PRINT_ERROR(err);
        return 1;
    }
    OrderNode* ord_match = ord_table[ord_idx];

    /* ── Step 2: O(1) user lookup via pointer table ──────────────── */
    int usr_idx = get_index_from_id(ord_match->data.user_id);
    if (usr_idx < 0 || usr_idx >= usr_max || !usr_table[usr_idx]) {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "User not found: %s", ord_match->data.user_id);
        free(usr_table);
        free(ord_table);
        free_delivery_boy_sll(boy_head);
        free_user_sll(usr_head);
        free_order_sll(ord_head);
        PRINT_ERROR(err);
        return 1;
    }
    UserNode* usr_match = usr_table[usr_idx];

    /* ── Step 3: Linear delivery boy lookup (no table available) ─── */
    char boy_name [MAX_STR_LEN];
    char boy_phone[MAX_STR_LEN];
    find_delivery_boy(boy_head, ord_match->data.delivery_boy_id,
                      boy_name, boy_phone);

    /*
     * ── Step 4: Print unified pipe-delimited receipt line ──────────
     *
     * FORMAT (13 fields after SUCCESS):
     *   SUCCESS|order_id|user_id|full_name|user_phone|user_email|
     *           address|slot|status|timestamp|boy_name|boy_phone|
     *           total|items_string
     *
     * NOTE: address is a raw comma-separated string from users.dat
     *       e.g. "No 11 - Flat No 11,Elumalai Street,West Tambaram,600045"
     *       Python splits on commas to render 4 address lines.
     */
    printf("SUCCESS|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%.2f|%s\n",
        ord_match->data.order_id,
        ord_match->data.user_id,
        usr_match->data.full_name,
        usr_match->data.phone,
        usr_match->data.email,
        usr_match->data.address,
        ord_match->data.delivery_slot,
        ord_match->data.status,
        ord_match->data.timestamp,
        boy_name,
        boy_phone,
        ord_match->data.total_amount,
        ord_match->data.items_string
    );

    /* ── Cleanup: tables first, then SLLs they point into ───────── */
    free(usr_table);
    free(ord_table);
    free_delivery_boy_sll(boy_head);
    free_user_sll(usr_head);
    free_order_sll(ord_head);

    return 0;
}
