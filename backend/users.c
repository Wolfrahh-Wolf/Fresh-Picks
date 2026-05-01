/*
 * users.c - Fresh Picks: User Management Logic
 * ===========================================================================
 * COMMANDS (argv[1]):
 *   list_users   [filter]   — all users, or filtered by "active" | "inactive"
 *   search_users <query>    — match user_id (exact) OR full_name (substring)
 *   get_user     <user_id>  — single user full profile
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "models.h"


/* Derive "Active" or "Inactive" from whether the password field is populated */
static const char* derive_status(const User* u) {
    return (u->password[0] != '\0') ? "Active" : "Inactive";
}

/* Copy src into dest as lowercase, safely bounded by n bytes */
static void str_to_lower(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) {
        dest[i] = (char)tolower((unsigned char)src[i]);
    }
    dest[i] = '\0';
}

/* Print one pipe-delimited user record: user_id|username|full_name|email|phone|address|status */
static void print_user_line(const User* u) {
    printf("%s|%s|%s|%s|%s|%s|%s\n",
        u->user_id,
        u->username,
        u->full_name,
        u->email,
        u->phone,
        u->address,
        derive_status(u)
    );
}


/* cmd_list_users — scan all table slots, count then print matching records */
void cmd_list_users(const char* filter,
                    UserNode **user_table, int user_table_size) {
    if (!user_table) { PRINT_ERROR("No users found or users.dat is missing"); return; }

    /* Prepare lowercase filter once outside the loop */
    char filter_lower[MAX_STR_LEN];
    if (filter) str_to_lower(filter_lower, filter, MAX_STR_LEN);

    /* First pass — count matching records */
    int match_count = 0;
    for (int i = 0; i < user_table_size; i++) {
        if (!user_table[i]) continue;
        if (!filter) {
            match_count++;
        } else {
            char status_lower[MAX_STR_LEN];
            str_to_lower(status_lower, derive_status(&user_table[i]->data), MAX_STR_LEN);
            if (strcmp(filter_lower, status_lower) == 0) match_count++;
        }
    }

    printf("SUCCESS|%d\n", match_count);

    /* Second pass — print each matching record */
    for (int i = 0; i < user_table_size; i++) {
        if (!user_table[i]) continue;
        if (!filter) {
            print_user_line(&user_table[i]->data);
        } else {
            char status_lower[MAX_STR_LEN];
            str_to_lower(status_lower, derive_status(&user_table[i]->data), MAX_STR_LEN);
            if (strcmp(filter_lower, status_lower) == 0)
                print_user_line(&user_table[i]->data);
        }
    }
}


/* cmd_search_users — O(1) for user_id hit; O(N) slot scan for full_name substring */
void cmd_search_users(const char* query,
                      UserNode **user_table, int user_table_size) {
    if (!query || strlen(query) == 0) { PRINT_ERROR("Search query cannot be empty"); return; }
    if (!user_table) { PRINT_ERROR("No users found or users.dat is missing"); return; }

    /* Assuming get_index_from_id handles basic format checking */
    int exact_idx = get_index_from_id(query); 
    
    /* If index is valid, verify it's an exact ID match (case-insensitive) */
    if (exact_idx >= 0 && exact_idx < user_table_size && user_table[exact_idx]) {
        char id_lower[MAX_ID_LEN];
        char query_lower[MAX_STR_LEN];
        str_to_lower(id_lower, user_table[exact_idx]->data.user_id, MAX_ID_LEN);
        str_to_lower(query_lower, query, MAX_STR_LEN);
        
        if (strcmp(id_lower, query_lower) == 0) {
            printf("SUCCESS|1\n");
            print_user_line(&user_table[exact_idx]->data);
            return; 
        }
    }

    /* ── FALLBACK: O(N) Substring Search for Names ── */
    char query_lower[MAX_STR_LEN];
    str_to_lower(query_lower, query, MAX_STR_LEN);

    int match_count = 0;
    
    for (int i = 0; i < user_table_size; i++) {
        if (!user_table[i]) continue;
        char name_lower[MAX_STR_LEN];
        str_to_lower(name_lower, user_table[i]->data.full_name, MAX_STR_LEN);
        if (strstr(name_lower, query_lower)) match_count++;
    }

    printf("SUCCESS|%d\n", match_count);

    for (int i = 0; i < user_table_size; i++) {
        if (!user_table[i]) continue;
        char name_lower[MAX_STR_LEN];
        str_to_lower(name_lower, user_table[i]->data.full_name, MAX_STR_LEN);
        if (strstr(name_lower, query_lower)) {
            print_user_line(&user_table[i]->data);
        }
    }
}


/* cmd_get_user — O(1) lookup by user_id via user_table */
void cmd_get_user(const char* user_id,
                  UserNode **user_table, int user_table_size) {
    if (!user_id || strlen(user_id) == 0) { PRINT_ERROR("user_id is required"); return; }
    if (!user_table) { PRINT_ERROR("No users found or users.dat is missing"); return; }

    int idx = get_index_from_id(user_id);
    if (idx < 0 || idx >= user_table_size || !user_table[idx]) {
        PRINT_ERROR("User not found");
        return;
    }

    UserNode *match = user_table[idx];
    printf("SUCCESS|%s|%s|%s|%s|%s|%s|%s\n",
        match->data.user_id,
        match->data.username,
        match->data.full_name,
        match->data.email,
        match->data.phone,
        match->data.address,
        derive_status(&match->data)
    );
}


/* main — load SLL, build table once, dispatch, free everything at the bottom */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        PRINT_ERROR("Usage: ./users <action> [args...]");
        return 1;
    }

    /* ── Load SLL (source of truth) ── */
    UserNode *user_head = load_user_sll();

    /* ── Build pointer table for O(1) access ── */
    int user_table_size = 0;
    UserNode **user_table = build_user_table(user_head, &user_table_size);

    if (!user_table) { PRINT_ERROR("Failed to build user table"); goto cleanup; }

    const char* action = argv[1];

    if (strcmp(action, "list_users") == 0) {
        /* argv: users list_users [filter]  — filter is optional */
        const char* filter = (argc >= 3) ? argv[2] : NULL;
        cmd_list_users(filter, user_table, user_table_size);

    } else if (strcmp(action, "search_users") == 0) {
        /* argv: users search_users <query>  argc >= 3 */
        if (argc < 3) { PRINT_ERROR("search_users requires a query argument"); goto cleanup; }
        cmd_search_users(argv[2], user_table, user_table_size);

    } else if (strcmp(action, "get_user") == 0) {
        /* argv: users get_user <user_id>  argc >= 3 */
        if (argc < 3) { PRINT_ERROR("get_user requires a user_id argument"); goto cleanup; }
        cmd_get_user(argv[2], user_table, user_table_size);

    } else {
        PRINT_ERROR("Unknown action. Valid: list_users, search_users, get_user");
    }

cleanup:
    /* ── Free table then SLL ── */
    free(user_table);
    free_user_sll(user_head);

    return 0;
}
