#include"StringUtilities.h"

// Check c-string to detemine if it contains
// only numbers
int checkNumber(const char* inputValue)
{
	int isNum = 1;
	const char* c = inputValue;
	
	// use is digit to determine if each character is a digit
	while (isNum && *c != '\0')
	{
		if (!isdigit(*c))
			isNum = 0;
		++c;
	}
	
	return isNum;
}
