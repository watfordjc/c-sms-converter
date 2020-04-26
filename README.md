# c-sms-converter
Convert text messages from gammu MySQL database to PDUs.

## Under Active Development
The current version has no database connectivity and can only produce the TP-PID, TP-DCS, TP-UDL, and TP-UD hex strings for a PDU. It takes a Big Endian UCS-2 hex string as input (`-d` or `--data` command line argument) and prints output that currently needs further parsing to be usable for a PDU.

The value of the `-d`/`--data` command line parameter is assumed to be the same format used by gammu MySQL database for the `text` database column (Big Endian UCS-2 hex string). If every 16 bit UCS-2 character is in the GSM 7-bit character set, the TP-UDL hex value is for septets and the TP-UD value is in GSM 7-bit compressed hexadecimal.

The TP-DCS value indicates how TP-UDL and TP-UD are to be treated, and TP-PID is currently hard-coded to 0x00 (standard SMS).
