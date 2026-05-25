/*
 * inventory.c - Fresh Picks: Admin Inventory Management
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h" 

/*
 * Print every product from the already-loaded veg SLL.
 * OUTPUT:  SUCCESS|
 *          veg_id|category|name|stock_g|price_per_1000g|tag|validity_days
 */
void cmd_list_products(VegNode* veg_head) {
    if (!veg_head) {
        printf("SUCCESS|\n");
        return;
    }

    printf("SUCCESS|\n");

    VegNode* curr = veg_head;
    while (curr != NULL) {
        printf("%s|%s|%s|%d|%.2f|%s|%d\n",
            curr->data.veg_id,
            curr->data.category,
            curr->data.name,
            curr->data.stock_g,
            curr->data.price_per_1000g,
            curr->data.tag,
            curr->data.validity_days
        );
        curr = curr->next;
    }
}


/* cmd_update_stock — O(1) lookup by veg_id, update fields, persist */
void cmd_update_stock(const char *veg_id, int new_stock_g,
                      float new_price, int new_validity,
                      VegNode **veg_table, int veg_table_size,
                      VegNode  *veg_head) {
    if (!veg_id || strlen(veg_id) == 0) { PRINT_ERROR("Vegetable ID required");        return; }
    if (new_stock_g  < 0)               { PRINT_ERROR("Stock cannot be negative");      return; }
    if (new_price   <= 0)               { PRINT_ERROR("Price must be greater than zero"); return; }
    if (new_validity < 1)               { PRINT_ERROR("Validity must be at least 1 day"); return; }
    if (!veg_table)                     { PRINT_ERROR("No vegetables found");           return; }

    int idx = get_index_from_id(veg_id);
    if (idx < 0 || idx >= veg_table_size || !veg_table[idx]) {
        PRINT_ERROR("Vegetable ID not found");
        return;
    }

    VegNode *match = veg_table[idx];
    match->data.stock_g         = new_stock_g;
    match->data.price_per_1000g = new_price;
    match->data.validity_days   = new_validity;

    save_veg_sll(veg_head);
    PRINT_SUCCESS("Stock updated successfully");
}


/* cmd_update_promo_stock — O(1) lookup by vf_id, update stock_g, persist */
void cmd_update_promo_stock(const char *vf_id, int new_stock_g,
                            FreeItemNode **free_table, int free_table_size,
                            FreeItemNode  *free_head) {
    if (!vf_id || strlen(vf_id) == 0) { PRINT_ERROR("Promo item ID required");       return; }
    if (new_stock_g < 0)              { PRINT_ERROR("Promo stock cannot be negative"); return; }
    if (!free_table)                  { PRINT_ERROR("No promo items found");           return; }

    int idx = get_index_from_id(vf_id);
    if (idx < 0 || idx >= free_table_size || !free_table[idx]) {
        PRINT_ERROR("Promo item ID not found");
        return;
    }

    FreeItemNode *match = free_table[idx];
    match->data.stock_g = new_stock_g;

    save_free_item_sll(free_head);
    PRINT_SUCCESS("Promo stock updated successfully");
}


/* cmd_list_promo — iterate all occupied free_table slots and print each record */
void cmd_list_promo(FreeItemNode **free_table, int free_table_size) {
    if (!free_table) { PRINT_ERROR("No promo items found"); return; }

    /* Print SUCCESS header first — bridge.py reads line 0 for status */
    printf("SUCCESS|\n");

    int count = 0;
    for (int i = 0; i < free_table_size; i++) {
        if (!free_table[i]) continue;
        printf("%s|%s|%d|%.2f|%d\n",
               free_table[i]->data.vf_id,
               free_table[i]->data.name,
               free_table[i]->data.stock_g,
               free_table[i]->data.min_trigger_amt,
               free_table[i]->data.free_qty_g);
        count++;
    }

    if (count == 0) {
        PRINT_ERROR("No promo items found");
    }
}


/* main — load SLLs, build tables once, dispatch, free everything at the bottom */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command. Usage: ./inventory <command> [args]");
        return 1;
    }

    /* ── Load SLLs (source of truth) ── */
    VegNode      *veg_head  = load_veg_sll();
    FreeItemNode *free_head = load_free_item_sll();

    /* ── Build pointer tables for O(1) access ── */
    int veg_table_size  = 0;
    int free_table_size = 0;
    VegNode      **veg_table  = build_veg_table(veg_head, &veg_table_size);
    FreeItemNode **free_table = build_free_table(free_head, &free_table_size);

    if (!veg_table)  { PRINT_ERROR("Failed to build veg table");  goto cleanup; }
    if (!free_table) { PRINT_ERROR("Failed to build free table"); goto cleanup; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list_products") == 0) {
        cmd_list_products(veg_head);
        
    } else if (strcmp(cmd, "update_stock") == 0) {
        /* argv: inventory update_stock <veg_id> <stock_g> <price> <validity>  argc >= 6 */
        if (argc < 6) { PRINT_ERROR("Usage: update_stock <veg_id> <stock_g> <price> <validity>"); goto cleanup; }
        cmd_update_stock(argv[2], atoi(argv[3]), atof(argv[4]), atoi(argv[5]),
                         veg_table, veg_table_size, veg_head);

    } else if (strcmp(cmd, "update_promo_stock") == 0) {
        /* argv: inventory update_promo_stock <vf_id> <new_stock_g>  argc >= 4 */
        if (argc < 4) { PRINT_ERROR("Usage: update_promo_stock <vf_id> <new_stock_g>"); goto cleanup; }
        cmd_update_promo_stock(argv[2], atoi(argv[3]),
                               free_table, free_table_size, free_head);

    } else if (strcmp(cmd, "list_promo") == 0) {
        /* argv: inventory list_promo  argc >= 2 */
        cmd_list_promo(free_table, free_table_size);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
    }

cleanup:
    /* ── Free tables then SLLs ── */
    free(veg_table);
    free(free_table);
    free_veg_sll(veg_head);
    free_free_item_sll(free_head);

    return 0;
}
