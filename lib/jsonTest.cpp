#include <iostream>
#include <boost/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
//using namespace std;
int main(){
	using namespace boost::posix_time;
	using namespace boost::gregorian;
	ptime now(second_clock::local_time());
	std::stringstream s;
	
	//s<< now.date("HHMMSS") << "-"<< now.time_of_day() << std::endl;
	//std::cout << s.str();

	std::string strTime = boost::posix_time::to_iso_string(boost::posix_time::second_clock::local_time());
	std::cout << strTime;
	system("pause");

}