#include "models.h"

#ifdef _WIN32
#include <process.h>
#define popen_cmd _popen
#define pclose_cmd _pclose
#define set_env_var(name, value) _putenv_s((name), (value))
#define get_pid _getpid
#else
#include <unistd.h>
#define popen_cmd popen
#define pclose_cmd pclose
#define set_env_var(name, value) setenv((name), (value), 1)
#define get_pid getpid
#endif

#define MAILER_BUF 4096

static int is_blank(const char *s) {
    if (!s) return 1;
    while (*s) {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return 0;
        s++;
    }
    return 1;
}

static void strip_newline(char *s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int write_powershell_script(const char *script_path) {
    FILE *fp = fopen(script_path, "w");
    if (!fp) return 0;

    fprintf(fp,
        "$ErrorActionPreference = 'Stop'\n"
        "try {\n"
        "  if ([string]::IsNullOrWhiteSpace($env:FP_EMAIL_USER) -or "
        "[string]::IsNullOrWhiteSpace($env:FP_EMAIL_APP_PASSWORD)) {\n"
        "    throw 'Email sender is not configured. Set FP_EMAIL_USER and FP_EMAIL_APP_PASSWORD.'\n"
        "  }\n"
        "  if ([string]::IsNullOrWhiteSpace($env:FP_MAILER_TO)) { throw 'Customer email address is missing.' }\n"
        "  if (-not (Test-Path -LiteralPath $env:FP_MAILER_PDF)) { throw 'Receipt PDF was not found.' }\n"
        "  $smtpHost = if ([string]::IsNullOrWhiteSpace($env:FP_SMTP_HOST)) { 'smtp.gmail.com' } else { $env:FP_SMTP_HOST }\n"
        "  $smtpPort = if ([string]::IsNullOrWhiteSpace($env:FP_SMTP_PORT)) { 587 } else { [int]$env:FP_SMTP_PORT }\n"
        "  $subject = 'Fresh Picks Receipt - ' + $env:FP_MAILER_ORDER_ID\n"
        "  $body = \"Hi $($env:FP_MAILER_NAME),`r`n`r`nYour Fresh Picks receipt for order $($env:FP_MAILER_ORDER_ID) is attached.`r`n`r`nThank you for shopping with Fresh Picks.`r`n\"\n"
        "  $securePassword = ConvertTo-SecureString $env:FP_EMAIL_APP_PASSWORD -AsPlainText -Force\n"
        "  $credential = New-Object System.Management.Automation.PSCredential($env:FP_EMAIL_USER, $securePassword)\n"
        "  Send-MailMessage -From $env:FP_EMAIL_USER -To $env:FP_MAILER_TO -Subject $subject -Body $body "
        "-SmtpServer $smtpHost -Port $smtpPort -UseSsl -Credential $credential -Attachments $env:FP_MAILER_PDF\n"
        "  Write-Output ('SUCCESS|Receipt emailed to ' + $env:FP_MAILER_TO)\n"
        "  exit 0\n"
        "} catch {\n"
        "  Write-Output ('ERROR|' + $_.Exception.Message)\n"
        "  exit 1\n"
        "}\n"
    );

    fclose(fp);
    return 1;
}

static void send_receipt(const char *to, const char *order_id,
                         const char *customer_name, const char *pdf_path) {
    char script_path[MAX_LINE_LEN];
    char command[MAX_LINE_LEN * 2];
    char line[MAILER_BUF];
    char parsed[MAILER_BUF] = "";
    const char *tmp_dir = getenv("TEMP");
    FILE *pipe;

    if (is_blank(to)) {
        PRINT_ERROR("Customer email address is missing.");
        return;
    }
    if (is_blank(pdf_path)) {
        PRINT_ERROR("Receipt PDF path is required.");
        return;
    }
    if (is_blank(getenv("FP_EMAIL_USER")) || is_blank(getenv("FP_EMAIL_APP_PASSWORD"))) {
        PRINT_ERROR("Email sender is not configured. Set FP_EMAIL_USER and FP_EMAIL_APP_PASSWORD.");
        return;
    }

    if (is_blank(tmp_dir)) tmp_dir = ".";

    set_env_var("FP_MAILER_TO", to);
    set_env_var("FP_MAILER_ORDER_ID", order_id);
    set_env_var("FP_MAILER_NAME", is_blank(customer_name) ? "Customer" : customer_name);
    set_env_var("FP_MAILER_PDF", pdf_path);

    snprintf(script_path, sizeof(script_path), "%s\\freshpicks_mailer_%d.ps1",
             tmp_dir, (int)get_pid());

    if (!write_powershell_script(script_path)) {
        PRINT_ERROR("Could not create mailer script.");
        return;
    }

    snprintf(command, sizeof(command),
             "powershell -NoProfile -ExecutionPolicy Bypass -File \"%s\" 2>&1",
             script_path);

    pipe = popen_cmd(command, "r");
    if (!pipe) {
        remove(script_path);
        PRINT_ERROR("Could not start PowerShell mailer.");
        return;
    }

    while (fgets(line, sizeof(line), pipe)) {
        strip_newline(line);
        if (strncmp(line, "SUCCESS|", 8) == 0 || strncmp(line, "ERROR|", 6) == 0) {
            strncpy(parsed, line, sizeof(parsed) - 1);
            parsed[sizeof(parsed) - 1] = '\0';
        }
    }

    pclose_cmd(pipe);
    remove(script_path);

    if (!is_blank(parsed)) {
        printf("%s\n", parsed);
    } else {
        PRINT_ERROR("Mailer finished without a valid response.");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        PRINT_ERROR("Usage: mailer send_receipt <to> <order_id> <customer_name> <pdf_path>");
        return 1;
    }

    if (strcmp(argv[1], "send_receipt") == 0) {
        if (argc < 6) {
            PRINT_ERROR("Usage: send_receipt <to> <order_id> <customer_name> <pdf_path>");
            return 1;
        }
        send_receipt(argv[2], argv[3], argv[4], argv[5]);
        return 0;
    }

    PRINT_ERROR("Unknown mailer action");
    return 1;
}
