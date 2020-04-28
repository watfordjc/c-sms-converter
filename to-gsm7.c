#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
/* MySQL */
#include <mysql.h>
/* libconfig */
#include <libconfig.h>

int main(int argc, char **argv);
int command_options(int argc, char **argv);
void open_config();
void parse_config();
void close_config();

int get_number_type(char *number);
void ascii_to_ucs2(char *str, char *ucs2);
int phone_number_to_hex(char *phone_number, char *hex);
void smsc_to_hex(char *smsc, char *smsc_hex);
int ucs2_to_gsm7(char *hexString, int len, char *str);
int gsm7_to_ud(char *str, int curChar, char *out7bit);
int parse_input(char *hexString);

int get_root_element_count(config_t *config, char *name, config_setting_t *config_element);
int get_element_count(config_setting_t *config, char *name, config_setting_t *config_element);
int get_config_int(config_setting_t *setting, char *name);
int get_config_bool(config_setting_t *setting, char *name);
const char *get_config_string(config_setting_t *setting, char *name);

void finish_with_error();
MYSQL *create_db_connection(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket);

long int GAMMU_VERSION = 17;

char *CONFIG_FILE = "";
struct config_t conf;
struct config_t *config;
MYSQL *db_conn;

void finish_with_error()
{
	fprintf(stderr, "%s\n", mysql_error(db_conn));
	mysql_close(db_conn);
	close_config();
	exit(1);
}

/* Detect phone number type of a character array. */
/*  Return values:
	-1 = NULL
	1 = International E.164 Format
	2 = National or Short Code Number Format
	3 = Non-Numeric Number (i.e. one-way text message)
*/
int get_number_type(char *number)
{
	int isE164 = 1;
	int isNumber = 1;
	int i = 0;
	if (number == NULL || strlen(number) == 0) {
		return -1;
	}
	if (number[i] != 0x2b) {
		isE164 = 0;
	}
	if ((isE164 == 0) && (number[i] < 0x30 || number[i] > 0x39)) {
		isNumber = 0;
	}
	if (isNumber == 0 || strlen(number) == 1) {
		return 3;
	}
	for (i = 1; i < strlen(number); i++) {
		if (number[i] > 0x2F && number[i] < 0x3A) {
			continue;
		} else {
			isE164 = 0;
			isNumber = 0;
			return 3;
		}
	}
	if (isE164 == 1 && isNumber == 1) {
		return 1;
	} else if (isE164 == 0 && isNumber == 1) {
		return 2;
	}
}

void ascii_to_ucs2(char *str, char *ucs2)
{
	int len = strlen(str);
	int j;
	char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < len; i++) {
		j = i * 4;
		ucs2[j] = '0';
		ucs2[j+1] = '0';
		ucs2[j+2] = hex[str[i] / 16];
		ucs2[j+3] = hex[str[i] % 16];
	}
}

int phone_number_to_hex(char *phone_number, char *hex)
{
	short is_odd;
	int len_nibbles, i;
	int len_number = strlen(phone_number);
	int number_type = get_number_type(phone_number);
	int first_digit = 0;
	int array_offset = 2;
	int is_number = 0;

	switch(number_type) {
		case -1:
			hex = "0100";
			return 0;
			break;
		case 1:
			hex[0] = '9';
			hex[1] = '1';
			first_digit = 1;
			is_number = 1;
			break;
		case 2:
			hex[0] = '8';
			hex[1] = '1';
			is_number = 1;
			break;
		case 3:
			hex[0] = 'D';
			hex[1] = '0';
			break;
	}

	if (is_number == 1) {
		array_offset -= first_digit;
		is_odd = (len_number - first_digit) % 2;
		for (i = first_digit; i < len_number; i+=2) {
			if (i + 1 == len_number && is_odd == 1) {
				hex[i + array_offset] = 'F';
			} else {
				hex[i + array_offset] = phone_number[i + 1];
			}
			hex[i + array_offset + 1] = phone_number[i];
		}
		len_nibbles = strlen(hex) - 2;
		return len_nibbles;
	} else {
		int j;
		char ucs2[len_number * 4 + 1];
		memset(ucs2, 0, len_number * 4 + 1);
		ascii_to_ucs2(phone_number, ucs2);
		char gsm7[len_number * 4 + 1];
		memset(gsm7, 0, len_number * 4 + 1);
		int curChar = ucs2_to_gsm7(ucs2, len_number, gsm7);
		if (curChar == -1) {
			fprintf(stderr, "Invalid Sender\n");
		}
		char out7bit[curChar];
		memset(out7bit, 0, curChar);
		j = gsm7_to_ud(gsm7, curChar, out7bit);
		for (i = 0; i < j; i++) {
			snprintf(hex + array_offset + (i * 2), 4, "%02X", out7bit[i]);
		}
		len_nibbles = strlen(hex) - 2;
		return len_nibbles;
	}

}

void smsc_to_hex(char *smsc_text, char *smsc)
{
	int len_nibbles = phone_number_to_hex(smsc_text, smsc);
	int smsc_len = len_nibbles / 2 + 1;
	printf("SMSC: 0x%02d%s\n", smsc_len, smsc);
}

void oa_to_hex(char *oa_text, char *oa)
{
	int len_nibbles = phone_number_to_hex(oa_text, oa);
	int oa_len = len_nibbles;
	if (oa[1] == '1' && oa[len_nibbles] == 'F') {
		oa_len--;
	}
	printf("TP-OA: 0x%02X%s\n", oa_len, oa);
}

void udh_to_hex(char *udh_text, char *udh)
{
	printf("UDH: %s\n", udh_text);
}

void scts_to_hex(char *scts_text, char *scts)
{
	time_t timestamp = atoi(scts_text);
	struct tm datetime;
	char date[24];
	memset(date, 0, 24);
	datetime = *gmtime(&timestamp);
	strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &datetime);

	if (date[4] != '-') {
		fprintf(stderr, "Y10K Bug?\n");
		exit(1);
	}

	scts[0] = date[3];
	scts[1] = date[2];
	scts[2] = date[6];
	scts[3] = date[5];
	scts[4] = date[9];
	scts[5] = date[8];
	scts[6] = date[12];
	scts[7] = date[11];
	scts[8] = date[15];
	scts[9] = date[14];
	scts[10] = date[18];
	scts[11] = date[17];
	scts[12] = '0';
	scts[13] = '0';
	printf("TP-SCTS: 0x%s\n", scts);
}

int main(int argc, char **argv)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	int i;
	int num_fields;
	unsigned long *field_lengths;
	int db_version;
	char *end_ptr;

	printf("MySQL client version: %s\n", mysql_get_client_info());
	command_options(argc, argv);
	open_config();
	parse_config();

	if (mysql_query(db_conn, "SELECT version FROM gammu")) {
		finish_with_error();
	}
	result = mysql_store_result(db_conn);
	if (result == NULL) {
		finish_with_error();
	}
	num_fields = mysql_num_fields(result);
	row = mysql_fetch_row(result);
	db_version = strtol(row[0], &end_ptr, 10);
	printf("Gammu database version %ld\n", db_version);
	if (db_version != GAMMU_VERSION) {
		fprintf(stderr, "Only Gammu database version %d is currently supported.\n", GAMMU_VERSION);
		exit(1);
	}
	if (mysql_query(db_conn, "SELECT *,UNIX_TIMESTAMP(ReceivingDateTime) FROM inbox WHERE udh != '' ORDER BY ID LIMIT 2")) {
		finish_with_error();
	}
	result = mysql_store_result(db_conn);
	if (result == NULL) {
		finish_with_error();
	}
	num_fields = mysql_num_fields(result);
	while (row = mysql_fetch_row(result)) {
		field_lengths = mysql_fetch_lengths(result);
		int smsc_number_type;
		char *field;
		int pduh = 0x04;
		for (int i = 0; i < num_fields; i++) {
			if (field_lengths[i] == 0) {
				field = row[i] ? "EMPTY" : "NULL";
			} else {
				field = row[i];
			}
			printf("%s ", field);
		}
		printf("\n");
		char *smsc_text = row[6];
//		char *smsc_text = "SMSTEST";
		char smsc[strlen(smsc_text) * 4];
		memset(smsc, 0, strlen(smsc_text) * 4);
		smsc_to_hex(smsc_text, smsc);
		char *scts_text = row[13];
		char scts[15];
		memset(scts, 0, 15);
		scts_to_hex(scts_text, scts);
		char *oa_text = row[3];
//		char *oa_text = "SMSTEST";
		char oa[strlen(oa_text) * 4];
		memset(oa, 0, strlen(oa_text) * 4);
		oa_to_hex(oa_text, oa);
		char *udh_text = row[5];
		if (udh_text != "") {
			char udh[strlen(udh_text) * 4];
			memset(udh, 0, strlen(udh_text) * 4);
			udh_to_hex(udh_text, udh);
			pduh |= 0x40;
		}
		printf("PDU-Header: 0x%02X\n", pduh);
//		printf("SMSC is %s of type %d.\n", row[6], smsc_number_type);
//		printf("Sender is %s of type %d.\n", row[3], get_number_type(row[3]));
		printf("Decoded Text: %s\n", row[8]);
		parse_input(row[2]);
	}
	mysql_free_result(result);
	mysql_close(db_conn);
	close_config();
}

void open_config()
{
	if (strlen(CONFIG_FILE) == 0)
	{
		fprintf(stderr, "No configuration file specified.\n");
		exit(1);
	}

	config = &conf;
	config_init(config);

	int loaded_config = config_read_file(config, CONFIG_FILE);
	if (loaded_config != 1)
	{
		fprintf(stderr, "Error reading config file %s. Error on line %d: %s\n", config_error_file(config), config_error_line(config), config_error_text(config));
		config_destroy(config);
	}
}

void parse_config()
{
	struct config_setting_t conf_mysql;
	struct config_setting_t *config_mysql = &conf_mysql;
	int mysql_count = get_root_element_count(config, "mysql", config_mysql);
	if (mysql_count != 1) {
		fprintf(stderr, "Configuration file contains %d mysql configurations. Number of configurations must be 1.\n", mysql_count);
		close_config();
		exit(1);
	}
	const char *host = NULL;
	const char *user = NULL;
	const char *passwd = NULL;
	const char *db = NULL;
	unsigned int port = 0;
	const char *unix_socket = NULL;
	struct config_setting_t *mysql_element = config_setting_get_elem(config_mysql, 0);
	host = get_config_string(mysql_element, "host");
	user = get_config_string(mysql_element, "user");
	passwd = get_config_string(mysql_element, "passwd");
	db = get_config_string(mysql_element, "db");
	port = get_config_int(mysql_element, "port");
	unix_socket = get_config_string(mysql_element, "unix_socket");
	create_db_connection(host, user, passwd, db, port, unix_socket);
}

void close_config()
{
	config_destroy(config);
}

MYSQL *create_db_connection(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket)
{
	db_conn = mysql_init(NULL);
	if (db_conn == NULL) {
		fprintf(stderr, "mysql_init() failed.\n");
		exit(1);
	}
	if (mysql_real_connect(db_conn, host, user, passwd, db, port, unix_socket, 0) == NULL) {
		finish_with_error();
	}
}

int command_options(int argc, char **argv)
{
	int c;
	while (1)
	{
		static struct option long_options[] =
		{
			{"data", required_argument, 0, 'd'},
			{"db_config", required_argument, 0, 1001},
			{0, 0, 0, 0}
		};

		int option_index = 0;
		c = getopt_long(argc, argv, "d:", long_options, &option_index);
		if (c == -1) {
			break;
		}

		switch(c)
		{
			case 0:
				if (long_options[option_index].flag != 0) {
					break;
				}
				break;
			case 'd':
				printf("Input: %s\n", optarg);
				parse_input(optarg);
				break;
			case 1001:
				printf("Database Configuration File: %s\n", optarg);
				CONFIG_FILE = optarg;
				break;
			default:
				break;
		}
	}
}

int ucs2_to_gsm7(char *hexString, int len, char *str)
{
	int i, j;
	char cur[7];
	cur[0] = '0';
	cur[1] = 'x';
	cur[6] = '\0';
	int num;
	int curChar = 0;

	for (i = 0; i < len; i++)
	{
		j = i * 4;
		cur[2] = hexString[j];
		cur[3] = hexString[j+1];
		cur[4] = hexString[j+2];
		cur[5] = hexString[j+3];
		num = (int)strtol(cur, NULL, 16);
		if ((num > 0x09 && num < 0x0b) || (num > 0x0c && num < 0x0e) || (num > 0x0d && num < 0x20) || (num > 0x23 && num < 0x25) || (num > 0x3f && num < 0x41) || (num > 0x5a && num < 0x61) || (num > 0x7a)) {
			switch (num) {
				case 0x40:
					num = 0x00;
					break;
				case 0xA3:
					num = 0x01;
					break;
				case 0x24:
					num = 0x02;
					break;
				case 0xA5:
					num = 0x03;
					break;
				case 0xE8:
					num = 0x04;
					break;
				case 0xE9:
					num = 0x05;
					break;
				case 0xF9:
					num = 0x06;
					break;
				case 0xEC:
					num = 0x07;
					break;
				case 0xF2:
					num = 0x08;
					break;
				case 0xC7:
					num = 0x09;
					break;
				case 0xD8:
					num = 0x0B;
					break;
				case 0xf8:
					num = 0x0C;
					break;
				case 0xc5:
					num = 0x0E;
					break;
				case 0xe5:
					num = 0x0F;
					break;
				case 0x0394:
					num = 0x10;
					break;
				case 0x5f:
					num = 0x11;
					break;
				case 0x03a6:
					num = 0x12;
					break;
				case 0x0393:
					num = 0x13;
					break;
				case 0x039b:
					num = 0x14;
					break;
				case 0x03a9:
					num = 0x15;
					break;
				case 0x03a0:
					num = 0x16;
					break;
				case 0x03a8:
					num = 0x17;
					break;
				case 0x03a3:
					num = 0x18;
					break;
				case 0x0398:
					num = 0x19;
					break;
				case 0x039e:
					num = 0x1A;
					break;
				case 0x0c:
					num = 0x1B0A;
					break;
				case 0x5e:
					num = 0x1B14;
					break;
				case 0x7b:
					num = 0x1B28;
					break;
				case 0x7d:
					num = 0x1B29;
					break;
				case 0x5c:
					num = 0x1B2F;
					break;
				case 0x5b:
					num = 0x1B3C;
					break;
				case 0x7e:
					num = 0x1B3D;
					break;
				case 0x5d:
					num = 0x1B3E;
					break;
				case 0x7c:
					num = 0x1B40;
					break;
				case 0x20ac:
					num = 0x1B65;
					break;
				default:
//					fprintf(stderr, "Error: Number %s is out of range - only UCS-2 and GSM 7-bit identical characters supported.\n", cur);
					return -1;
			}
//			exit(1);
		}
		if (num > 0xffffff) {
			str[curChar] = (num & 0xFF000000) >> 24;
			curChar++;
		}
		if (num > 0xffff) {
			str[curChar] = (num & 0xFF0000) >> 16;
			curChar++;
		}
		if (num > 0xff) {
//			printf("%s = 0x%04X & 0xFF00 = 0x%02X\n", cur, num, (num & 0xFF00) >> 8);
//			printf("%s = %c\n", cur, (num & 0xFF00) >> 8);
			str[curChar] = (num & 0xFF00) >> 8;
			curChar++;
		}
//		printf("%s = 0x%04X & 0xFF = 0x%02X\n", cur, num, num & 0xFF);
//		printf("%s = %c\n", cur, num & 0xFF);
		str[curChar] = num & 0xFF;
		curChar++;
	}
	printf("\n");
	printf("%s\n", str);
	return curChar;

}

int gsm7_to_ud(char *str, int curChar, char *out7bit)
{
	int i = 0;
	int j = 0;
	int shiftAt = 0;
	for (i = 0; i < curChar; i++) {
		if ((i > 0) && (i + 1) % 8 == 0) {
			shiftAt = 0;
			continue;
		}
//		printf("0x%02X, 0x%02X = ", str[i], str[i+1]);
		char temp = str[i]>>shiftAt;
		char temp2 = '\0';
		if (i + 1 < curChar) {
			temp2 = str[i+1]<<7-shiftAt;
		} else {
//			printf("Last character = ");
		}
		out7bit[j] = temp + temp2;
//		printf("0x%02X + 0x%02X = 0x%02X\n", str[i], str[i+1], temp + temp2);
		shiftAt++;
		j++;
	}
	return j;
}


int parse_input(char *hexString)
{
	int i, j;
	int len = strlen(hexString) / 4;
	char str[strlen(hexString)];
	memset(str, 0, strlen(hexString));
	int curChar = ucs2_to_gsm7(hexString, len, str);

	printf("TP-MTI: TODO\n");
	printf("TP-MMS: TODO\n");
	printf("TP-SRI: TODO\n");
	printf("TP-RP: TODO\n");
	printf("TP-UDHI: TODO\n");
	printf("TP-PID: 0x00\n");

	if (curChar == -1) {
		printf("UCS-2\n");
		printf("TP-DCS: 0x08\n");
		printf("TP-UDL: 0x%02X\n", len * 2);
		printf("TP-UD: 0x%s\n", hexString);
		return 0;
	} else {
		char out7bit[curChar];
		memset(out7bit, 0, curChar);
		printf("7-bit GSM\n");
		printf("TP-DCS: 0x00\n");
		j = gsm7_to_ud(str, curChar, out7bit);
		printf("TP-UDL: 0x%02X\n", len);
		printf("TP-UD: 0x");
		for (i = 0; i < j; i++) {
			printf("%02X", out7bit[i]);
		}
		printf("\n");
	}

}


/*
* Function get_root_element_count:
*	* Returns number (int) of elements in list 'name' in configuration 'config'.
*	* Updates pointer '*config_element' to point to element 'name'.
*
* config_t *config : pointer to parsed config
* char *name : pointer to name of element
* config_setting_t *conf_element : pointer to element
*/
int get_root_element_count(config_t *config, char *name, config_setting_t *config_element)
{
	config_setting_t *conf_element = config_lookup(config, name);
	if (conf_element == NULL)
	{
		fprintf(stderr, "No %s found in configuration file.\n", name);
		exit(1);
	}
	else
	{
		*config_element = *conf_element;
		return config_setting_length(config_element);
	}
}

/*
* Function get_element_count:
*	* Returns number (int) of elements in list 'name' in configuration setting 'config_setting'.
*	* Updates pointer '*config_element' to point to element 'name'.
*
* config_setting_t *config : pointer to parsed config setting
* char *name : pointer to name of element
* config_setting_t *conf_element : pointer to element
*/
int get_element_count(config_setting_t *config_setting, char *name, config_setting_t *config_element)
{
	config_setting_t *conf_element = config_setting_lookup(config_setting, name);
	if (conf_element == NULL)
	{
		fprintf(stderr, "No %s found in configuration file for this schema.\n", name);
		return 0;
	}
	else {
		*config_element = *conf_element;
		return config_setting_length(config_element);
	}
}

/*
* Function get_config_int looks up the integer value of 'name'
*  in the configuration setting 'setting' and returns the integer.
* -1 is returned if 'name' does not exist.
*/
int get_config_int(config_setting_t *setting, char *name)
{
	config_setting_t *setting_pointer = NULL;
	setting_pointer = config_setting_lookup(setting, name);
	if (setting_pointer != NULL)
	{
		return config_setting_get_int(setting_pointer);
	}
	else
	{
		return -1;
	}
}

/*
* Function get_config_bool looks up the boolean value of 'name'
*  in the configuration setting 'setting' and returns it as an integer.
* -1 is returned if 'name' does not exist.
*/
int get_config_bool(config_setting_t *setting, char *name)
{
	config_setting_t *setting_pointer = NULL;
	setting_pointer = config_setting_lookup(setting, name);
	if (setting_pointer != NULL)
	{
		return config_setting_get_bool(setting_pointer);
	}
	else
	{
		return -1;
	}
}

/*
* Function get_config_string looks up the string value of 'name'
*  in the configuration setting 'setting' and returns the string.
* NULL is returned if 'name' does not exist.
*/
const char *get_config_string(config_setting_t *setting, char *name)
{
	config_setting_t *setting_pointer = NULL;
	setting_pointer = config_setting_lookup(setting, name);
	if (setting_pointer != NULL)
	{
		return config_setting_get_string(setting_pointer);
	}
	else
	{
		return NULL;
	}
}
