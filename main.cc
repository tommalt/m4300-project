#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <vector>
#include <string>
#include <iostream>

#define DATE_FMT "%Y-%m-%d"
#define DATE_KEY "Date"
#define DATA_SEP ','

#define DEFAULT_VARIANCE 0.10
#define DEFAULT_MEAN_RETURN 0.05
#define DEFAULT_TCOST_MODEL TCostPerTrade
#define DEFAULT_TCOST_MODEL_NAME "Per trade transaction costs"
#define DEFAULT_TCOST 10.0

/* different ways of computing transaction costs */
enum TCostModel {
	TCostNULL,
	TCostPerTrade,
	TCostPerShare,
};
enum EModels {
	ModelNULL,
	MeanVar,
	/* more models go here ...*/
};

static time_t parse_date(char const *s);
static std::vector<std::vector<double> > read_stock_data(std::vector<std::string> const & filepaths);
static time_t strtotime(char const *s);               /* convert date time string to an integral representation (time_t) */
static time_t read_until(FILE *file, time_t begin);     /* read from data source until we hit a certain point in time (begin) */
static int indexOf(char const *line, char const *field);
static void die(char const *fmt, ...);               /* print a message and kill the program */
static void warn(char const *fmt, ...);              /* print a message */
static void usage(char const * argv0);

time_t parse_date(char const *s)
{
	struct tm tm;
	memset(&tm, 0, sizeof tm);
	if (!strptime(s, DATE_FMT, &tm))
		return 0;
	return mktime(&tm);
}
std::vector<std::vector<double> >
read_stock_data(std::vector<std::string> const & filepaths)
{
	std::vector<std::vector<double> > data;
	std::vector<double> prices;
	char buf[256];
	char *p;
	for (auto const & fp : filepaths) {
		char const *f = fp.c_str();
		FILE *file = fopen(f, "r");
		if (!file) {
			perror("getstocks");
			die("failed to open file: %s\nAborting\n", f);
		}
		fgets(buf, sizeof buf, file);
		int close_index = indexOf(buf, "Adj. Close");
		while (fgets(buf, sizeof buf, file)) {
			p = buf;
			for (int i = 0; i < close_index; i++) {
				p = strchr(p, DATA_SEP);
				p++;
			}
			char *endptr;
			double price = strtod(p, &endptr);
			if (price == 0.0 && endptr == p) { /* a parse error ocurred */
				perror("Parsing Adj. Close");
				die("Aborting\n");
			}
			prices.push_back(price);
		}
		data.emplace_back(prices);
	}
	return data;
}
time_t strtotime(char const *s)
{
	struct tm tm;
	memset(&tm, 0, sizeof tm);
	if (!strptime(s, DATE_FMT, &tm))
		return 0;
	return mktime(&tm);
}
/*
 * read_until
 *   @file  := FILE stream
 *   @begin := the date we want to synchronize with
 * returns:
 *   the earliest time observation that is >= begin, in Unix time.
 *   or 0 (1970-01-01 00:00:00) if there is no date >= begin
 */
time_t read_until(FILE *file, time_t begin)
{
	char buf[256];
	char *date;
	int date_index;
	int nread;
	struct tm tm;
	time_t tmp;

	/* find 'date' field in the header */
	if (!fgets(buf, sizeof buf, file))
		die("File is empty\n");
	date_index = indexOf(buf, "date");

	/* iterate over dates in file until date >= begin */
	while (fgets(buf, sizeof buf, file)) {
		nread = strlen(buf);
		date = buf;
		for (int i = 0; i < date_index; i++) {
			date = strchr(date, DATA_SEP);
			if (!date) {
				die("Date field not found in data\n");
			}
			date++;
		}
		memset(&tm, 0, sizeof tm);
		if (!strptime(date, DATE_FMT, &tm)) {
			die("Failed to parse date in file\n");
		}
		tmp = mktime(&tm);
		if (tmp >= begin) {
			fseek(file, SEEK_CUR, -nread);
			return tmp;
		}
	}
	/* we reached EOF without finding a date >= begin
	 * let caller know by returning 0 */
	return 0;
}
/*
 * Find the index of a field in a comma-sperated line of text
 * ex)
 *      line = "Date,Open,High,Low,Close"
 *      indexOf(line, "Low") = 3
 */
int indexOf(char const *line, char const *field)
{
	int index;
	char const *begin, *end, *lineEnd;

	begin = line;
	end = strchr(begin, DATA_SEP);
	if (end == NULL) {
		if (strcmp(begin, field) == 0)
			return 0;
		die("Field (%s) not found in string: %s\n", field, line);
	}
	lineEnd = begin + strlen(begin);
	index = 0;
	while (begin < lineEnd) {
		int size = end - begin;
		if (strncasecmp(begin, field, size) == 0) {
			break;
		}
		begin = end + 1;
		end = strchrnul(begin, DATA_SEP);
		index++;
	}
	int nsep = 0;
	for (char const *tmp = line; *tmp != '\0'; tmp++) {
		if (*tmp == DATA_SEP)
			nsep++;
	}
	if (index > nsep)
		die("Field not found in data\n");
	return index;
}
void die(char const *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
	exit(1);
}
void warn(char const *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
}
void usage(char const *argv0)
{
	printf(
	"Usage: %s [-h|--help] [-c $$$] [-t <ps|pt> $$$] [-m models...]\n"
	"          [-v variance] [-r return]\n"
	"    -h,--help           show this help message\n"
	"    -c $$$              initial capital\n"
	"    -t pt|ps $$$        transaction cost model. see below\n"
	"    -m models           names of the models to use. See list below\n"
	"    -v variance         Maximum portfolio variance, in percentage form (decimal)\n"
	"    -r return           Minimum portfolio mean return, in percentage form (decimal)\n"
	"See below for info on default values and input data\n"
	"\n"
	"Transaction Costs (-t)\n"
	"    'pt' means 'per trade' and 'ps' means 'per share'\n"
	"    when specifying transaction costs with -t, the first argument should be\n"
	"    one of these two abbreviations. The second argument should be the value to\n"
	"    use for the transaction costs.\n"
	"    ex)\n"
	"        -t pt 10.0 == transaction costs of 10 dollars per trade\n"
	"        -t ps 0.05 == transaction costs of 5 cents per share\n"
	"\n"
	"Models (-m)\n"
	"    The models currently implemented are:\n"
	"        Markowitz Mean-Variance = meanvar\n"
	"    example:\n"
	"        -m meanvar\n"
	"    Specifies that the program should do Mean-Variance optimization\n"
	"    Multiple models can be specified\n"
	"\n"
	"Variance (-v)\n"
	"    The value shall be specified in decimal notation.\n"
	"    For example, a variance of 8 percent should be specified as 0.08\n"
	"\n"
	"Returns (-r)\n"
	"    Just like variance, specify in decimal notation.\n"
	"\n"
	"Default values\n"
	"    If the command options are not specified, the following defaults will be assumed:\n"
	"        Variance = %.2f\n"
	"        Mean return = %.2f\n"
	"        Transaction cost model = %s\n"
	"        Transaction costs = %.2f\n"
	"\n"
	"Input Data\n"
	"    From its standard input, the program reads:\n"
	"        a starting date\n"
	"        an ending date\n"
	"        and a list of filenames\n"
	"    The files must be in CSV format, with column labels\n"
	"\n"
	"Example usage (using the getstock program to get the data)\n"
	"    $ ./getstock -k apikey -b 2018-01-01 -e 2018-04-01 -o data -- JPM BAC GS | %s -c 100000 -t pt 10.0 -m meanvar -v 0.04 -r 0.07\n"
	,argv0
	,DEFAULT_VARIANCE
	,DEFAULT_MEAN_RETURN
	,DEFAULT_TCOST_MODEL_NAME
	,DEFAULT_TCOST
	,argv0);
	exit(1);
}
int main(int argc, char **argv)
{
	std::vector<EModels> models;
	double initial_capital;
	double variance;
	double mean_return;
	double tcost;
	enum TCostModel tcm;

	initial_capital = 0.0;
	variance = 0.0;
	mean_return = 0.0;
	tcost = 0.0;
	tcm = TCostNULL;

	char const *argv0 = argv[0];
	int ac;
	char **av;
	for (ac = argc - 1, av = argv + 1; ac && *av
		&& av[0][0] == '-' && av[0][1]; ac--, av++) {
		if (av[0][1] == '-' && av[0][2] == '\0') {
			ac--; av++;
			break;
		}
		char *opt, *tmp, *endptr;  /* endptr for strtod(3) */
		int brk_ = 0;
		for (opt = (*av) + 1; *opt && !brk_; opt++) {
			switch (*opt) {
			case 'c':
				tmp = (opt[1] != '\0') ? (opt + 1) : (--ac, *(++av));
				initial_capital = strtod(tmp, &endptr);
				if (initial_capital == 0.0 && endptr == tmp) {
					perror("strtod:");
					die("Failed to parse initial capital: %s\n", tmp);
				}
				brk_ = 1;
				break;
			case 't':
				tmp = (opt[1] != '\0') ? (opt + 1) : (--ac, *(++av));
				if (strcmp(tmp, "ps") == 0) {
					tcm = TCostPerShare;
				} else if (strcmp(tmp, "pt") == 0) {
					tcm = TCostPerTrade;
				} else {
					warn("Transaction cost model must be \"ps\" or \"pt\".\n");
					usage(argv0);
				}
				--ac; ++av;
				tmp = *av;
				if (*tmp == '-') {
					warn("Transaction cost option is missing its value.\n");
					usage(argv0);
				}
				tcost = strtod(tmp, &endptr);
				if (tcost == 0.0 && endptr == tmp) {
					perror("strtod:");
					die("Failed to parse transaction cost value: %s\n", tmp);
				}
				brk_ = 1;
				break;
			case 'm':
				tmp = (opt[1] != '\0') ? (opt + 1) : (--ac, *(++av));
				while (tmp != NULL && *tmp != '-') {
					if (strcmp(tmp, "meanvar") == 0) {
						models.push_back(MeanVar);
					} else {
						die("Unknown model name: %s\n", tmp);
					}
					--ac; ++av;
					tmp = *av;
				}
				if (tmp != NULL && *tmp == '-') {
					++ac; --av;
				}
				brk_ = 1;
				break;
			case 'v':
				tmp = (opt[1] != '\0') ? (opt + 1) : (--ac, *(++av));
				variance = strtod(tmp, &endptr);
				if (variance == 0.0 && endptr == tmp) {
					die("Failed to parse variance: %s\n", tmp);
				}
				brk_ = 1;
				break;
			case 'r':
				tmp = (opt[1] != '\0') ? (opt + 1) : (--ac, *(++av));
				mean_return = strtod(tmp, &endptr);
				if (mean_return == 0.0 && endptr == tmp) {
					die("Failed to parse mean_return: %s\n", tmp);
				}
				brk_ = 1;
				break;
			case 'h':
				usage(argv0);
			default:
				usage(argv0);
			}
		}
	}
	if (models.empty()) {
		warn("No models specified, using default of Markowitz Mean-variance\n");
		models.push_back(MeanVar);
	}
	if (tcm == TCostNULL) {
		warn("No transaction cost model specified, using default of %.2f per trade\n", DEFAULT_TCOST);
		tcm = TCostPerTrade;
		tcost = DEFAULT_TCOST;
	}
	if (variance == 0.0) {
		warn("Variance not specified. Using default value %.4f\n", DEFAULT_VARIANCE);
		variance = DEFAULT_VARIANCE;
	}
	if (mean_return == 0.0) {
		warn("Mean return not specified. Using default value %.4f\n", DEFAULT_MEAN_RETURN);
		mean_return = DEFAULT_MEAN_RETURN;
	}

	std::string begin_date;
	std::string end_date;
	time_t begin, end;

	std::cin >> begin_date;
	std::cin >> end_date;
	begin = parse_date(begin_date.c_str());
	end = parse_date(end_date.c_str());
	if (begin == 0) {
		die("Error parsing date: %s\n", begin_date.c_str());
	}
	if (end == 0) {
		die("Error parsing date: %s\n", end_date.c_str());
	}
	std::vector<std::string> files;
	std::string tmp;
	while (std::cin >> tmp) {
		files.emplace_back(tmp);
	}
	auto data = read_stock_data(files);
	return 0;
}