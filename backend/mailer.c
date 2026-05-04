/*
 * mailer.c — Fresh Picks: SMTP Mailer for Receipts and OTPs
 * =========================================================
 * Sends either:
 *   1. an order receipt PDF to a customer, or
 *   2. an OTP email for user verification flows.
 * Uses libcurl's curl_mime API for safe, RFC-compliant multipart
 * attachment — no manual MIME boundary construction needed.
 *
 * USAGE:
 *   ./mailer <recipient_email> <absolute_path_to_pdf>
 *   ./mailer otp <recipient_email> <otp_code> <register|cancel_order> [reference]
 *
 * OUTPUT CONTRACT (parsed by bridge.py):
 *   SUCCESS|Email sent
 *   ERROR|<reason>
 *
 * COMPILE:
 *   gcc -Wall -Wextra -o mailer mailer.c -lcurl
 *   (Add to build.sh: gcc -Wall -Wextra -o mailer mailer.c -lcurl)
 *
 * SETUP — Gmail App Password:
 *   1. Enable 2-Factor Authentication on the sender Gmail account.
 *   2. Go to: myaccount.google.com → Security → App Passwords
 *   3. Generate a password for "Mail" / "Other (Custom)".
 *   4. Replace SMTP_USERNAME and SMTP_PASSWORD below with your
 *      Gmail address and the 16-character App Password respectively.
 *   NOTE: App Passwords bypass 2FA and work with plain SMTP.
 *         Never use your real Gmail password here.
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>


/* ═══════════════════════════════════════════════════════════════
   SECTION 1: SMTP CREDENTIALS — Replace before deployment
   ═══════════════════════════════════════════════════════════════ */

#define SMTP_HOST       "smtps://smtp.gmail.com:465"
#define SMTP_USERNAME   "codecrafters658@gmail.com"   /* ← REPLACE */
#define SMTP_PASSWORD   "bvdw hcww ahsh vhev"            /* ← REPLACE with App Password */
#define SENDER_NAME     "FreshPicks Orders"
#define SENDER_EMAIL    "codecrafters658@gmail.com"   /* ← REPLACE (same as username) */


/* ═══════════════════════════════════════════════════════════════
   SECTION 2: EMAIL CONTENT TEMPLATES
   ═══════════════════════════════════════════════════════════════ */

#define RECEIPT_EMAIL_SUBJECT   "FreshPicks Order Confirmed - Receipt Attached"
#define OTP_EXPIRY_MINUTES      10

/* Plain-text body shown in email clients that don't render HTML */
#define RECEIPT_EMAIL_BODY_TEXT \
    "Dear Customer,\r\n\r\n"                                        \
    "Thank you for shopping with FreshPicks!\r\n\r\n"               \
    "Your payment has been received and your order has been "       \
    "confirmed successfully.\r\n\r\n"                               \
    "Please find your order receipt attached to this email as a "   \
    "PDF document.\r\n\r\n"                                         \
    "Our team is now preparing your order for delivery. You can "   \
    "check the latest status from your FreshPicks account.\r\n\r\n" \
    "If you have any questions about your order, please contact "   \
    "us at support@freshpicks.in or call +91 98765 43210.\r\n\r\n"  \
    "Thank you for choosing FreshPicks.\r\n\r\n"                    \
    "Happy shopping,\r\n"                                           \
    "The FreshPicks Team\r\n"                                       \
    "123 Grocery Ave, Market City, Chennai - 600045\r\n"

/* HTML body (shown in modern email clients) */
#define RECEIPT_EMAIL_BODY_HTML \
    "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">" \
    "<p>Dear Customer,</p>"                                           \
    "<p>Thank you for shopping with <strong>FreshPicks</strong>! 🧺</p>" \
    "<p>Your payment has been received and your order has been "       \
    "<strong>confirmed successfully</strong>.</p>"                     \
    "<p>Please find your <strong>order receipt</strong> attached to "  \
    "this email as a PDF document.</p>"                               \
    "<p>Our team is now preparing your order for delivery. You can "   \
    "check the latest status from your FreshPicks account at any "     \
    "time.</p>"                                                        \
    "<p>If you have any questions, contact us at "                    \
    "<a href=\"mailto:support@freshpicks.in\">support@freshpicks.in</a>"  \
    " or call <strong>+91 98765 43210</strong>.</p>"                  \
    "<p>Thank you for choosing FreshPicks.</p>"                       \
    "<br><p>Happy shopping,<br>"                                      \
    "<strong>The FreshPicks Team</strong><br>"                        \
    "<small style=\"color:#888;\">123 Grocery Ave, Market City, "    \
    "Chennai - 600045</small></p>"                                    \
    "</body></html>"


/* ═══════════════════════════════════════════════════════════════
   SECTION 3: ARGUMENT VALIDATION
   ═══════════════════════════════════════════════════════════════ */

/*
 * validate_args — checks argument count and basic sanity.
 * Returns 0 on success, 1 on failure (caller prints ERROR and exits).
 */
static int is_valid_email(const char* email) {
    return email && strlen(email) > 0 && strchr(email, '@') != NULL;
}

static int validate_receipt_args(int argc,
                                 const char* recipient,
                                 const char* pdf_path) {
    return argc != 3 || !is_valid_email(recipient) || strlen(pdf_path) == 0;
}

static int validate_otp_args(const char* recipient,
                             const char* otp_code,
                             const char* purpose) {
    if (!is_valid_email(recipient) || strlen(otp_code) == 0) {
        return 1;
    }
    if (strcmp(purpose, "register") != 0 &&
        strcmp(purpose, "cancel_order") != 0) {
        return 1;
    }
    return 0;
}

static int build_otp_email(const char* purpose,
                           const char* otp_code,
                           const char* reference,
                           char* subject,
                           size_t subject_size,
                           char* body_text,
                           size_t body_text_size,
                           char* body_html,
                           size_t body_html_size) {
    const char* safe_reference =
        (reference && strlen(reference) > 0) ? reference : "your order";

    if (strcmp(purpose, "register") == 0) {
        snprintf(subject, subject_size, "FreshPicks Registration OTP");
        snprintf(body_text, body_text_size,
            "Dear Customer,\r\n\r\n"
            "Thank you for registering with FreshPicks.\r\n\r\n"
            "Your verification OTP is: %s\r\n\r\n"
            "This OTP is valid for %d minutes.\r\n"
            "If you did not request this registration, please ignore this email.\r\n\r\n"
            "Regards,\r\n"
            "The FreshPicks Team\r\n",
            otp_code, OTP_EXPIRY_MINUTES);
        snprintf(body_html, body_html_size,
            "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
            "<p>Dear Customer,</p>"
            "<p>Thank you for registering with <strong>FreshPicks</strong>.</p>"
            "<p>Your verification OTP is:</p>"
            "<div style=\"display:inline-block;padding:12px 18px;border-radius:8px;"
            "background:#f4f8fb;border:1px solid #d4e4f2;font-size:28px;"
            "letter-spacing:0.25em;font-weight:700;color:#007acc;\">%s</div>"
            "<p style=\"margin-top:18px;\">This OTP is valid for <strong>%d minutes</strong>.</p>"
            "<p>If you did not request this registration, you can ignore this email.</p>"
            "<p>Regards,<br><strong>The FreshPicks Team</strong></p>"
            "</body></html>",
            otp_code, OTP_EXPIRY_MINUTES);
        return 0;
    }

    if (strcmp(purpose, "cancel_order") == 0) {
        snprintf(subject, subject_size, "FreshPicks Cancellation OTP - %s",
                 safe_reference);
        snprintf(body_text, body_text_size,
            "Dear Customer,\r\n\r\n"
            "We received a request to cancel order %s.\r\n\r\n"
            "Your cancellation OTP is: %s\r\n\r\n"
            "This OTP is valid for %d minutes.\r\n"
            "If you did not request this cancellation, please ignore this email and keep your account secure.\r\n\r\n"
            "Regards,\r\n"
            "The FreshPicks Team\r\n",
            safe_reference, otp_code, OTP_EXPIRY_MINUTES);
        snprintf(body_html, body_html_size,
            "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
            "<p>Dear Customer,</p>"
            "<p>We received a request to cancel <strong>%s</strong>.</p>"
            "<p>Your cancellation OTP is:</p>"
            "<div style=\"display:inline-block;padding:12px 18px;border-radius:8px;"
            "background:#fff5f5;border:1px solid #f0c2c2;font-size:28px;"
            "letter-spacing:0.25em;font-weight:700;color:#dc3545;\">%s</div>"
            "<p style=\"margin-top:18px;\">This OTP is valid for <strong>%d minutes</strong>.</p>"
            "<p>If you did not request this cancellation, please ignore this email.</p>"
            "<p>Regards,<br><strong>The FreshPicks Team</strong></p>"
            "</body></html>",
            safe_reference, otp_code, OTP_EXPIRY_MINUTES);
        return 0;
    }

    return 1;
}


/* ═══════════════════════════════════════════════════════════════
   SECTION 4: CURL SEND FUNCTION
   ═══════════════════════════════════════════════════════════════ */

/*
 * send_email_message — core libcurl function.
 *
 * Uses curl_mime API (libcurl >= 7.56.0) to build a multipart/mixed
 * email with a text part, an HTML part, and an optional PDF attachment.
 * smtps:// (port 465) uses implicit TLS — no STARTTLS needed.
 *
 * PARAMS:
 *   recipient_email — e.g. "user@example.com"
 *   subject         — visible email subject
 *   body_text       — plain-text fallback body
 *   body_html       — HTML body for modern clients
 *   attachment_path — absolute path to the PDF file on disk, or NULL
 *
 * RETURNS:
 *   CURLE_OK (0) on success, non-zero CURLcode on failure.
 */
static CURLcode send_email_message(const char* recipient_email,
                                   const char* subject,
                                   const char* body_text,
                                   const char* body_html,
                                   const char* attachment_path)
{
    CURL       *curl;
    CURLcode    res       = CURLE_FAILED_INIT;
    curl_mime  *mime      = NULL;
    curl_mimepart *part   = NULL;
    struct curl_slist *recipients = NULL;

    /* ── Build the RFC 5322 "From" and "To" header strings ── */
    char from_header[256];
    char to_header[256];
    snprintf(from_header, sizeof(from_header),
             "%s <%s>", SENDER_NAME, SENDER_EMAIL);
    snprintf(to_header, sizeof(to_header), "%s", recipient_email);

    curl = curl_easy_init();
    if (!curl) return CURLE_FAILED_INIT;

    /* ── SMTP server and credentials ── */
    curl_easy_setopt(curl, CURLOPT_URL,            SMTP_HOST);
    curl_easy_setopt(curl, CURLOPT_USERNAME,        SMTP_USERNAME);
    curl_easy_setopt(curl, CURLOPT_PASSWORD,        SMTP_PASSWORD);

    /* ── TLS: use SSL for the entire connection (implicit TLS, port 465) ── */
    curl_easy_setopt(curl, CURLOPT_USE_SSL,         CURLUSESSL_ALL);

    /*
     * SSL peer verification:
     * Set to 1L in production to verify Gmail's certificate chain.
     * Set to 0L only for local testing if you hit SSL errors.
     */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2L);

    /* ── Envelope: who the SMTP server sees as sender/recipient ── */
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM,       SENDER_EMAIL);
    recipients = curl_slist_append(recipients, recipient_email);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT,       recipients);

    /* ── Build the MIME message ── */
    mime = curl_mime_init(curl);

    /* Part 1: Plain text alternative */
    part = curl_mime_addpart(mime);
    curl_mime_data(part, body_text, CURL_ZERO_TERMINATED);
    curl_mime_type(part, "text/plain; charset=utf-8");

    /* Part 2: HTML alternative */
    part = curl_mime_addpart(mime);
    curl_mime_data(part, body_html, CURL_ZERO_TERMINATED);
    curl_mime_type(part, "text/html; charset=utf-8");

    if (attachment_path && strlen(attachment_path) > 0) {
        /* Optional PDF attachment — curl reads directly from the file path */
        part = curl_mime_addpart(mime);
        curl_mime_filedata(part, attachment_path);
        curl_mime_type(part, "application/pdf");
        curl_mime_filename(part, "FreshPicks_Receipt.pdf");
        curl_mime_encoder(part, "base64");
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    /* ── Custom headers: Subject, From, To (displayed in email client) ── */
    struct curl_slist *headers = NULL;
    char subject_header[256];
    char from_display[256];
    char to_display[256];
    snprintf(subject_header, sizeof(subject_header),
             "Subject: %s", subject);
    snprintf(from_display,   sizeof(from_display),
             "From: %s", from_header);
    snprintf(to_display,     sizeof(to_display),
             "To: %s", to_header);

    headers = curl_slist_append(headers, subject_header);
    headers = curl_slist_append(headers, from_display);
    headers = curl_slist_append(headers, to_display);
    headers = curl_slist_append(headers, "MIME-Version: 1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* ── Send ── */
    res = curl_easy_perform(curl);

    /* ── Cleanup (always runs, even on failure) ── */
    curl_slist_free_all(recipients);
    curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    return res;
}


/* ═══════════════════════════════════════════════════════════════
   SECTION 5: main()
   ═══════════════════════════════════════════════════════════════ */

int main(int argc, char* argv[]) {
    const char* recipient_email = NULL;
    CURLcode    res             = CURLE_FAILED_INIT;

    if (argc == 3) {
        const char* pdf_path = argv[2];

        if (validate_receipt_args(argc, argv[1], pdf_path)) {
            printf("ERROR|Usage: ./mailer <recipient_email> <absolute_pdf_path>\n");
            return 1;
        }

        recipient_email = argv[1];
        curl_global_init(CURL_GLOBAL_DEFAULT);
        res = send_email_message(
            recipient_email,
            RECEIPT_EMAIL_SUBJECT,
            RECEIPT_EMAIL_BODY_TEXT,
            RECEIPT_EMAIL_BODY_HTML,
            pdf_path
        );

    } else if (argc >= 5 && strcmp(argv[1], "otp") == 0) {
        const char* otp_code  = argv[3];
        const char* purpose   = argv[4];
        const char* reference = (argc > 5) ? argv[5] : "";
        char subject[256];
        char body_text[2048];
        char body_html[4096];

        if (validate_otp_args(argv[2], otp_code, purpose)) {
            printf("ERROR|Usage: ./mailer otp <recipient_email> <otp_code> <register|cancel_order> [reference]\n");
            return 1;
        }
        if (build_otp_email(purpose, otp_code, reference,
                            subject, sizeof(subject),
                            body_text, sizeof(body_text),
                            body_html, sizeof(body_html))) {
            printf("ERROR|Unsupported OTP purpose\n");
            return 1;
        }

        recipient_email = argv[2];
        curl_global_init(CURL_GLOBAL_DEFAULT);
        res = send_email_message(
            recipient_email,
            subject,
            body_text,
            body_html,
            NULL
        );

    } else {
        printf("ERROR|Usage: ./mailer <recipient_email> <absolute_pdf_path> OR ./mailer otp <recipient_email> <otp_code> <register|cancel_order> [reference]\n");
        return 1;
    }

    /* ── Cleanup global state ── */
    curl_global_cleanup();

    /* ── Report result in SUCCESS|data / ERROR|reason format ── */
    if (res == CURLE_OK) {
        printf("SUCCESS|Email sent\n");
        return 0;
    } else {
        printf("ERROR|%s\n", curl_easy_strerror(res));
        return 1;
    }
}
