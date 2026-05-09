/*
 * txt_to_bin_converter.c - Fresh Picks: One-Time Text-to-Binary Migration Tool
 * ==============================================================================
 * Standalone C program. Reads every pipe-delimited .txt source file, remaps
 * all IDs to the new 4-digit standard (e.g. U001 → U1001, D001 → D1001),
 * and writes flat binary struct sequences into the corresponding .dat files.
 *
 * IMPORTANT: Run this ONCE on the machine that holds the .txt files.
 *            After migration, the .txt files are no longer used by any binary.
 *            orders.txt is intentionally SKIPPED — start fresh from an empty
 *            orders.dat, as orders will be placed through the live system.
 *
 * FILES CONVERTED:
 *   users.txt           → users.dat           (User structs)
 *   admin_creds.txt     → admin_creds.dat      (AdminCreds structs)
 *   products.txt        → products.dat         (Vegetable structs)
 *   free_inventory.txt  → free_inventory.dat   (FreeItem structs)
 *   delivery_boys.txt   → delivery_boys.dat    (DeliveryBoy structs)
 *
 * FILES SKIPPED:
 *   orders.txt — orders.dat will begin empty; new orders go through the system.
 *
 * ID REMAPPING RULES:
 *   U001   → U1001   (users)
 *   A1001  → A1001   (admins — already 4-digit, no change)
 *   V1001  → V1001   (vegetables — already 4-digit, no change)
 *   VF101  → VF1001  (free items — 3-digit → 4-digit)
 *   D001   → D1001   (delivery boys — 3-digit → 4-digit)
 *
 * COMPILE:
 *   gcc -Wall -Wextra -o txt_to_bin_converter txt_to_bin_converter.c utils.c -lm
 *
 * USAGE:
 *   ./txt_to_bin_converter
 *   (Run from inside the backend/ directory where the .txt files live.)
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   SECTION 1: ID REMAPPING HELPERS

   These functions normalise old 3-digit IDs to the new 4-digit
   format. They write the remapped ID into `out` (must be at
   least MAX_ID_LEN bytes).

   Strategy: parse the numeric suffix, then reformat with prefix.
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: remap_user_id
 * PURPOSE:  Convert "U001" → "U1001", "U002" → "U1002", etc.
 *           Already-4-digit IDs ("U1001") pass through unchanged.
 * PARAMS:   old_id — original ID string, out — output buffer (MAX_ID_LEN)
 */
static void remap_user_id(const char* old_id, char* out) {
    int num = 0;
    /* Extract the numeric part after the "U" prefix */
    sscanf(old_id + 1, "%d", &num);

    /* If number is already in the 4-digit range (≥ 1000), keep as-is */
    if (num >= 1000) {
        strncpy(out, old_id, MAX_ID_LEN - 1);
        out[MAX_ID_LEN - 1] = '\0';
        return;
    }

    /* Remap: U001 (num=1) → U1001 */
    snprintf(out, MAX_ID_LEN, "U%d", 1000 + num);
}

/*
 * FUNCTION: remap_free_item_id
 * PURPOSE:  Convert "VF101" → "VF1001", "VF102" → "VF1002", etc.
 *           Already-4-digit IDs ("VF1001") pass through unchanged.
 * PARAMS:   old_id — original ID string, out — output buffer (MAX_ID_LEN)
 */
static void remap_free_item_id(const char* old_id, char* out) {
    int num = 0;
    /* Extract the numeric part after the "VF" prefix */
    sscanf(old_id + 2, "%d", &num);

    if (num >= 1000) {
        strncpy(out, old_id, MAX_ID_LEN - 1);
        out[MAX_ID_LEN - 1] = '\0';
        return;
    }

    /* Remap: VF101 (num=101) → VF1001 */
    snprintf(out, MAX_ID_LEN, "VF%d", 1000 + (num - 100));
}

/*
 * FUNCTION: remap_delivery_id
 * PURPOSE:  Convert "D001" → "D1001", "D002" → "D1002", etc.
 *           Already-4-digit IDs ("D1001") pass through unchanged.
 * PARAMS:   old_id — original ID string, out — output buffer (MAX_ID_LEN)
 */
static void remap_delivery_id(const char* old_id, char* out) {
    int num = 0;
    sscanf(old_id + 1, "%d", &num);

    if (num >= 1000) {
        strncpy(out, old_id, MAX_ID_LEN - 1);
        out[MAX_ID_LEN - 1] = '\0';
        return;
    }

    snprintf(out, MAX_ID_LEN, "D%d", 1000 + num);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: CONVERSION FUNCTIONS (one per entity)
   Each function reads the .txt source, parses pipe-delimited
   lines, populates a struct, and fwrites it to the .dat file.
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: convert_users
 * PURPOSE:  Parse users.txt and write User structs to users.dat.
 *           Remaps U001/U002/U003 → U1001/U1002/U1003.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|N users converted  OR  ERROR|reason (to stdout)
 * SCHEMA:   users.txt: user_id|username|password|full_name|email|phone|address
 */
static void convert_users(void) {
    FILE* in = fopen("users.txt", "r");
    if (!in) { PRINT_ERROR("Cannot open users.txt"); return; }

    FILE* out = fopen(USERS_FILE, "wb");
    if (!out) { fclose(in); PRINT_ERROR("Cannot create users.dat"); return; }

    char line[MAX_LINE_LEN];
    int  count = 0;

    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        User u;
        memset(&u, 0, sizeof(User));

        char* tok = strtok(line, "|");
        if (!tok) continue;
        remap_user_id(tok, u.user_id);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(u.username,  tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(u.password,  tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(u.full_name, tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(u.email,     tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(u.phone,     tok, MAX_STR_LEN - 1);

        /* Address is the remainder of the line (may contain commas) */
        tok = strtok(NULL, "\n"); if (!tok) continue;
        strncpy(u.address,   tok, MAX_ADD_LEN - 1);

        fwrite(&u, sizeof(User), 1, out);
        count++;
    }

    fclose(in);
    fclose(out);

    char msg[MAX_STR_LEN];
    snprintf(msg, sizeof(msg), "%d users converted → users.dat", count);
    PRINT_SUCCESS(msg);
}


/*
 * FUNCTION: convert_admin
 * PURPOSE:  Parse admin_creds.txt and write AdminCreds structs to admin_creds.dat.
 *           Admin IDs (A1001, A1002) are already 4-digit — no remapping needed.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|N admins converted  OR  ERROR|reason
 * SCHEMA:   admin_creds.txt: admin_id|username|password|admin_name|email
 */
static void convert_admin(void) {
    FILE* in = fopen("admin_creds.txt", "r");
    if (!in) { PRINT_ERROR("Cannot open admin_creds.txt"); return; }

    FILE* out = fopen(ADMIN_FILE, "wb");
    if (!out) { fclose(in); PRINT_ERROR("Cannot create admin_creds.dat"); return; }

    char line[MAX_LINE_LEN];
    int  count = 0;

    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        AdminCreds a;
        memset(&a, 0, sizeof(AdminCreds));

        char* tok = strtok(line, "|");
        if (!tok) continue;
        /* A1001/A1002 are already in the correct 4-digit format */
        strncpy(a.admin_id,   tok, MAX_ID_LEN  - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(a.username,   tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(a.password,   tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(a.admin_name, tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(a.email,      tok, MAX_STR_LEN - 1);

        fwrite(&a, sizeof(AdminCreds), 1, out);
        count++;
    }

    fclose(in);
    fclose(out);

    char msg[MAX_STR_LEN];
    snprintf(msg, sizeof(msg), "%d admins converted → admin_creds.dat", count);
    PRINT_SUCCESS(msg);
}


/*
 * FUNCTION: convert_products
 * PURPOSE:  Parse products.txt and write Vegetable structs to products.dat.
 *           Vegetable IDs (V1001–V1048) are already 4-digit — no remapping.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|N products converted  OR  ERROR|reason
 * SCHEMA:   products.txt: veg_id|category|name|stock_g|price_per_1000g|tag|validity_days
 */
static void convert_products(void) {
    FILE* in = fopen("products.txt", "r");
    if (!in) { PRINT_ERROR("Cannot open products.txt"); return; }

    FILE* out = fopen(PRODUCTS_FILE, "wb");
    if (!out) { fclose(in); PRINT_ERROR("Cannot create products.dat"); return; }

    char line[MAX_LINE_LEN];
    int  count = 0;

    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        Vegetable v;
        memset(&v, 0, sizeof(Vegetable));

        char* tok = strtok(line, "|");
        if (!tok) continue;
        strncpy(v.veg_id,   tok, MAX_ID_LEN  - 1);  /* V1001 — already 4-digit */

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(v.category, tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(v.name,     tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        v.stock_g = atoi(tok);

        tok = strtok(NULL, "|"); if (!tok) continue;
        v.price_per_1000g = atof(tok);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(v.tag,      tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|");
        v.validity_days = tok ? atoi(tok) : 1;

        fwrite(&v, sizeof(Vegetable), 1, out);
        count++;
    }

    fclose(in);
    fclose(out);

    char msg[MAX_STR_LEN];
    snprintf(msg, sizeof(msg), "%d products converted → products.dat", count);
    PRINT_SUCCESS(msg);
}


/*
 * FUNCTION: convert_free_inventory
 * PURPOSE:  Parse free_inventory.txt and write FreeItem structs to free_inventory.dat.
 *           Remaps VF101/VF102 → VF1001/VF1002.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|N free items converted  OR  ERROR|reason
 * SCHEMA:   free_inventory.txt: vf_id|name|stock_g|min_trigger_amt|free_qty_g
 */
static void convert_free_inventory(void) {
    FILE* in = fopen("free_inventory.txt", "r");
    if (!in) { PRINT_ERROR("Cannot open free_inventory.txt"); return; }

    FILE* out = fopen(FREE_INV_FILE, "wb");
    if (!out) { fclose(in); PRINT_ERROR("Cannot create free_inventory.dat"); return; }

    char line[MAX_LINE_LEN];
    int  count = 0;

    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        FreeItem fi;
        memset(&fi, 0, sizeof(FreeItem));

        char* tok = strtok(line, "|");
        if (!tok) continue;
        remap_free_item_id(tok, fi.vf_id);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(fi.name,  tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        fi.stock_g = atoi(tok);

        tok = strtok(NULL, "|"); if (!tok) continue;
        fi.min_trigger_amt = atof(tok);

        tok = strtok(NULL, "|");
        fi.free_qty_g = tok ? atoi(tok) : 0;

        fwrite(&fi, sizeof(FreeItem), 1, out);
        count++;
    }

    fclose(in);
    fclose(out);

    char msg[MAX_STR_LEN];
    snprintf(msg, sizeof(msg), "%d free items converted → free_inventory.dat", count);
    PRINT_SUCCESS(msg);
}


/*
 * FUNCTION: convert_delivery_boys
 * PURPOSE:  Parse delivery_boys.txt and write DeliveryBoy structs to delivery_boys.dat.
 *           Remaps D001/D002/D003 → D1001/D1002/D1003.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|N delivery boys converted  OR  ERROR|reason
 * SCHEMA:   delivery_boys.txt: boy_id|name|phone|vehicle_no|is_active|last_assigned
 */
static void convert_delivery_boys(void) {
    FILE* in = fopen("delivery_boys.txt", "r");
    if (!in) { PRINT_ERROR("Cannot open delivery_boys.txt"); return; }

    FILE* out = fopen(DELIVERY_FILE, "wb");
    if (!out) { fclose(in); PRINT_ERROR("Cannot create delivery_boys.dat"); return; }

    char line[MAX_LINE_LEN];
    int  count = 0;

    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        DeliveryBoy db;
        memset(&db, 0, sizeof(DeliveryBoy));

        char* tok = strtok(line, "|");
        if (!tok) continue;
        remap_delivery_id(tok, db.boy_id);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(db.name,       tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(db.phone,      tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        strncpy(db.vehicle_no, tok, MAX_STR_LEN - 1);

        tok = strtok(NULL, "|"); if (!tok) continue;
        db.is_active = atoi(tok);

        tok = strtok(NULL, "|");
        db.last_assigned = tok ? atoi(tok) : 0;

        fwrite(&db, sizeof(DeliveryBoy), 1, out);
        count++;
    }

    fclose(in);
    fclose(out);

    char msg[MAX_STR_LEN];
    snprintf(msg, sizeof(msg), "%d delivery boys converted → delivery_boys.dat", count);
    PRINT_SUCCESS(msg);
}


/*
 * FUNCTION: create_empty_orders_dat
 * PURPOSE:  Create an empty orders.dat so the runtime system has a valid
 *           file to fopen() on the first order. Zero bytes = zero records.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|orders.dat initialised (empty)  OR  ERROR|reason
 */
static void create_empty_orders_dat(void) {
    FILE* fp = fopen(ORDERS_FILE, "wb");
    if (!fp) { PRINT_ERROR("Cannot create orders.dat"); return; }
    fclose(fp);
    PRINT_SUCCESS("orders.dat initialised (empty — place orders through the live system)");
}


/* ═════════════════════════════════════════════════════════════
   MAIN — Run all conversions in sequence
   ═════════════════════════════════════════════════════════════ */
int main(void) {
    printf("================================================\n");
    printf("  Fresh Picks — txt → binary (.dat) Converter  \n");
    printf("  Run this ONCE from inside backend/            \n");
    printf("================================================\n\n");

    printf("[1/6] Converting users...\n");
    convert_users();

    printf("[2/6] Converting admin credentials...\n");
    convert_admin();

    printf("[3/6] Converting products (vegetables)...\n");
    convert_products();

    printf("[4/6] Converting free inventory...\n");
    convert_free_inventory();

    printf("[5/6] Converting delivery boys...\n");
    convert_delivery_boys();

    printf("[6/6] Initialising empty orders.dat...\n");
    create_empty_orders_dat();

    printf("\n================================================\n");
    printf("  Migration complete. All .dat files are ready.\n");
    printf("  You may now start Flask: python app.py\n");
    printf("================================================\n");

    return 0;
}
