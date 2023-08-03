// IFC String decoding
// https://technical.buildingsmart.org/resources/ifcimplementationguidance/string-encoding/
// http://www.steptools.com/stds/step/IS_final_p21e3.html


#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <codecvt>
#include <locale>

using namespace std;

namespace webifc::parsing {

    
    void encodeCharacters(std::stringstream &stream,std::string &data) 
    {
        std::u16string utf16 = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(data.data());
        stream << "\\X2\\" << std::hex <<std::setfill('0') << std::uppercase;
        for (char16_t uC : utf16) stream << std::setw(4) << static_cast<int>(uC);
        stream << std::dec<< std::setw(0) << "\\X0\\";
    }

    std::string p21encode(std::string_view input) 
      {   
        std::stringstream stream;
        std::string tmp;
        bool inEncode=false;
        for (char c : input) {
          if (c > 126 || c < 32) { 
            if (!inEncode) 
            {
              inEncode = true;
              tmp=c;
              continue;
            } else {
              tmp+=c;
              continue;
            }
          } else {
            if (inEncode) {
                encodeCharacters(stream,tmp);
                inEncode=false;
                tmp.clear();
            }
          }
          stream << c;
        }
        if (inEncode) encodeCharacters(stream,tmp);
        return stream.str();
    }


    struct p21decoder
    {
        explicit p21decoder(const vector<char>& source) : p21(source), index(0), error(false), codepage(0) {
            decode();
        }

        vector<char> & unescape() {
            return result;
        }

        bool has_error() const {
            return error;
        }

    private:
        void decode() {
            while (!is_end() && !error) {
                char c = next();
                switch (c) {
                    case '\'':
                        if (is_end()) return;
                        c = next();
                        if (c == '\'')
                            result.push_back(c);
                        else
                            error = true;
                        break;

                    case '\\':
                        if (is_end()) return;
                        parse_escape();
                        break;

                    default:
                        result.push_back(c);
                        break;
                }
            }
        }

        void parse_escape() {
            char c = next();
            switch (c) {
                case '\\':
                    result.push_back('\\');
                    break;
                
                case 'X':
                    parse_escape_x();
                    break;

                case 'S':
                    parse_s();
                    break;

                case 'P':
                    parse_p();
                    break;

                default:
                    error = true;
                    break;
            }
        }

        void parse_escape_x() {
            if (is_end()) return;
            char c = next();
            switch (c) {
                case '\\':
                    parse_x();
                    break;

                case '2':
                    parse_x2();
                    break;

                case '4':
                    parse_x4();
                    break;
                    
                default:
                    error = true;
                    break;
            }
        }

        void parse_s() {
            if (!try_read('\\')) return;

            char c = next();
            const char * bytes = iso8859_to_utf(c);
            for (int i = 0; i < 3; i++)
            {
                if (bytes[i] == 0) break;
                result.push_back(bytes[i]);
            }
        }

        void parse_p() {
            char c = next();
            if (!try_read('\\')) return;
            if (c < 'A' || c > 'I') {
                error = true;
                return;
            }
            codepage = c - 'A';
        }

        void parse_x() {
            char c = next();
            char d1 = parse_hex(c);

            if (is_end()) return;
            c = next();
            char d2 = parse_hex(c);

            char str[2];
            str[0] = (d1 << 4) | d2;
            str[1] = 0;
            u16string u16str(reinterpret_cast<char16_t*>(str), 1);
            std::wstring_convert<codecvt_utf8_utf16<char16_t>,char16_t> convert; 
            string utf8 = convert.to_bytes(u16str);
            copy(utf8.begin(), utf8.end(), back_inserter(result));
        }

        void parse_x2() {
            if (!try_read('\\')) return;

            vector<char> bytes;
            while (!is_end() && !error) {
                if (read_to_x0(bytes, 2)) break;
            }

            // bytes swap. convert little endian to big
            for (uint32_t i = 0; i < bytes.size(); i+=2) {
                char c = bytes[i];
                bytes[i] = bytes[i+1];
                bytes[i+1] = c;
            }

            u16string u16str(reinterpret_cast<char16_t*>(&bytes[0]), bytes.size() / 2);
            std::wstring_convert<codecvt_utf8_utf16<char16_t>,char16_t> convert; 
            string utf8 = convert.to_bytes(u16str);
            copy(utf8.begin(), utf8.end(), back_inserter(result));
        }
        
        void parse_x4() {
            if (!try_read('\\')) return;
            
            vector<char> bytes;
            while (!is_end() && !error) {
                if (read_to_x0(bytes, 4)) break;
            }

            // bytes swap. convert little endian to big
            for (uint32_t i = 0; i < bytes.size(); i+=4) {
                char c = bytes[i];
                bytes[i] = bytes[i+3];
                bytes[i+3] = c;
                c = bytes[i+1];
                bytes[i+1] = bytes[i+2];
                bytes[i+2] = c;
            }
            
            u32string u32str(reinterpret_cast<char32_t*>(&bytes[0]), bytes.size() / 4);
            std::wstring_convert<codecvt_utf8<char32_t>,char32_t> convert; 
            string utf8 = convert.to_bytes(u32str);
            copy(utf8.begin(), utf8.end(), back_inserter(result));
        }

        bool read_to_x0(vector<char> & bytes, int count) {
            while (!is_end() && count > 0) {
                char c = next();
                if (c == '\\') {
                    if (try_read('X') && try_read('0') && try_read('\\'))
                        return true;
                    return false;
                }

                char d1 = parse_hex(c);
                
                if (is_end()) return false;
                c = next();
                char d2 = parse_hex(c);

                bytes.push_back((d1 << 4) | d2);
                count--;
            }
            return false;
        }

        inline bool is_end() const {
            return index >= p21.size();
        }

        inline bool try_read(char value) {
            if (is_end()) {
                error = true;
                return false;
            }
            char c = next();
            if (c == value) return true;

            error = true;
            return false;
        }

        inline char next() {
            if (index < p21.size()) return p21[index++];
            return ' ';
        }

        char parse_hex(char value) {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            error = true;
            return 0;
        }

        const char* iso8859_to_utf(unsigned char symbol) {
            // Converting tables ISO 8859-x -> UTF-8
            // Each table has utf-8 bytes for symbols 0xa1 .. 0xff
            static const char table[9][95][3] = {{{-62,-95,0},{-62,-94,0},{-62,-93,0},{-62,-92,0},{-62,-91,0},{-62,-90,0},{-62,-89,0},{-62,-88,0},{-62,-87,0},{-62,-86,0},{-62,-85,0},{-62,-84,0},{-62,-83,0},{-62,-82,0},{-62,-81,0},{-62,-80,0},{-62,-79,0},{-62,-78,0},{-62,-77,0},{-62,-76,0},{-62,-75,0},{-62,-74,0},{-62,-73,0},{-62,-72,0},{-62,-71,0},{-62,-70,0},{-62,-69,0},{-62,-68,0},{-62,-67,0},{-62,-66,0},{-62,-65,0},{-61,-128,0},{-61,-127,0},{-61,-126,0},{-61,-125,0},{-61,-124,0},{-61,-123,0},{-61,-122,0},{-61,-121,0},{-61,-120,0},{-61,-119,0},{-61,-118,0},{-61,-117,0},{-61,-116,0},{-61,-115,0},{-61,-114,0},{-61,-113,0},{-61,-112,0},{-61,-111,0},{-61,-110,0},{-61,-109,0},{-61,-108,0},{-61,-107,0},{-61,-106,0},{-61,-105,0},{-61,-104,0},{-61,-103,0},{-61,-102,0},{-61,-101,0},{-61,-100,0},{-61,-99,0},{-61,-98,0},{-61,-97,0},{-61,-96,0},{-61,-95,0},{-61,-94,0},{-61,-93,0},{-61,-92,0},{-61,-91,0},{-61,-90,0},{-61,-89,0},{-61,-88,0},{-61,-87,0},{-61,-86,0},{-61,-85,0},{-61,-84,0},{-61,-83,0},{-61,-82,0},{-61,-81,0},{-61,-80,0},{-61,-79,0},{-61,-78,0},{-61,-77,0},{-61,-76,0},{-61,-75,0},{-61,-74,0},{-61,-73,0},{-61,-72,0},{-61,-71,0},{-61,-70,0},{-61,-69,0},{-61,-68,0},{-61,-67,0},{-61,-66,0},{-61,-65,0}},{{-60,-124,0},{-53,-104,0},{-59,-127,0},{-62,-92,0},{-60,-67,0},{-59,-102,0},{-62,-89,0},{-62,-88,0},{-59,-96,0},{-59,-98,0},{-59,-92,0},{-59,-71,0},{-62,-83,0},{-59,-67,0},{-59,-69,0},{-62,-80,0},{-60,-123,0},{-53,-101,0},{-59,-126,0},{-62,-76,0},{-60,-66,0},{-59,-101,0},{-53,-121,0},{-62,-72,0},{-59,-95,0},{-59,-97,0},{-59,-91,0},{-59,-70,0},{-53,-99,0},{-59,-66,0},{-59,-68,0},{-59,-108,0},{-61,-127,0},{-61,-126,0},{-60,-126,0},{-61,-124,0},{-60,-71,0},{-60,-122,0},{-61,-121,0},{-60,-116,0},{-61,-119,0},{-60,-104,0},{-61,-117,0},{-60,-102,0},{-61,-115,0},{-61,-114,0},{-60,-114,0},{-60,-112,0},{-59,-125,0},{-59,-121,0},{-61,-109,0},{-61,-108,0},{-59,-112,0},{-61,-106,0},{-61,-105,0},{-59,-104,0},{-59,-82,0},{-61,-102,0},{-59,-80,0},{-61,-100,0},{-61,-99,0},{-59,-94,0},{-61,-97,0},{-59,-107,0},{-61,-95,0},{-61,-94,0},{-60,-125,0},{-61,-92,0},{-60,-70,0},{-60,-121,0},{-61,-89,0},{-60,-115,0},{-61,-87,0},{-60,-103,0},{-61,-85,0},{-60,-101,0},{-61,-83,0},{-61,-82,0},{-60,-113,0},{-60,-111,0},{-59,-124,0},{-59,-120,0},{-61,-77,0},{-61,-76,0},{-59,-111,0},{-61,-74,0},{-61,-73,0},{-59,-103,0},{-59,-81,0},{-61,-70,0},{-59,-79,0},{-61,-68,0},{-61,-67,0},{-59,-93,0},{-53,-103,0}},{{-60,-90,0},{-53,-104,0},{-62,-93,0},{-62,-92,0},{-17,-97,-75},{-60,-92,0},{-62,-89,0},{-62,-88,0},{-60,-80,0},{-59,-98,0},{-60,-98,0},{-60,-76,0},{-62,-83,0},{-17,-97,-74},{-59,-69,0},{-62,-80,0},{-60,-89,0},{-62,-78,0},{-62,-77,0},{-62,-76,0},{-62,-75,0},{-60,-91,0},{-62,-73,0},{-62,-72,0},{-60,-79,0},{-59,-97,0},{-60,-97,0},{-60,-75,0},{-62,-67,0},{-17,-97,-73},{-59,-68,0},{-61,-128,0},{-61,-127,0},{-61,-126,0},{-17,-97,-72},{-61,-124,0},{-60,-118,0},{-60,-120,0},{-61,-121,0},{-61,-120,0},{-61,-119,0},{-61,-118,0},{-61,-117,0},{-61,-116,0},{-61,-115,0},{-61,-114,0},{-61,-113,0},{-17,-97,-71},{-61,-111,0},{-61,-110,0},{-61,-109,0},{-61,-108,0},{-60,-96,0},{-61,-106,0},{-61,-105,0},{-60,-100,0},{-61,-103,0},{-61,-102,0},{-61,-101,0},{-61,-100,0},{-59,-84,0},{-59,-100,0},{-61,-97,0},{-61,-96,0},{-61,-95,0},{-61,-94,0},{-17,-97,-70},{-61,-92,0},{-60,-117,0},{-60,-119,0},{-61,-89,0},{-61,-88,0},{-61,-87,0},{-61,-86,0},{-61,-85,0},{-61,-84,0},{-61,-83,0},{-61,-82,0},{-61,-81,0},{-17,-97,-69},{-61,-79,0},{-61,-78,0},{-61,-77,0},{-61,-76,0},{-60,-95,0},{-61,-74,0},{-61,-73,0},{-60,-99,0},{-61,-71,0},{-61,-70,0},{-61,-69,0},{-61,-68,0},{-59,-83,0},{-59,-99,0},{-53,-103,0}},{{-60,-124,0},{-60,-72,0},{-59,-106,0},{-62,-92,0},{-60,-88,0},{-60,-69,0},{-62,-89,0},{-62,-88,0},{-59,-96,0},{-60,-110,0},{-60,-94,0},{-59,-90,0},{-62,-83,0},{-59,-67,0},{-62,-81,0},{-62,-80,0},{-60,-123,0},{-53,-101,0},{-59,-105,0},{-62,-76,0},{-60,-87,0},{-60,-68,0},{-53,-121,0},{-62,-72,0},{-59,-95,0},{-60,-109,0},{-60,-93,0},{-59,-89,0},{-59,-118,0},{-59,-66,0},{-59,-117,0},{-60,-128,0},{-61,-127,0},{-61,-126,0},{-61,-125,0},{-61,-124,0},{-61,-123,0},{-61,-122,0},{-60,-82,0},{-60,-116,0},{-61,-119,0},{-60,-104,0},{-61,-117,0},{-60,-106,0},{-61,-115,0},{-61,-114,0},{-60,-86,0},{-60,-112,0},{-59,-123,0},{-59,-116,0},{-60,-74,0},{-61,-108,0},{-61,-107,0},{-61,-106,0},{-61,-105,0},{-61,-104,0},{-59,-78,0},{-61,-102,0},{-61,-101,0},{-61,-100,0},{-59,-88,0},{-59,-86,0},{-61,-97,0},{-60,-127,0},{-61,-95,0},{-61,-94,0},{-61,-93,0},{-61,-92,0},{-61,-91,0},{-61,-90,0},{-60,-81,0},{-60,-115,0},{-61,-87,0},{-60,-103,0},{-61,-85,0},{-60,-105,0},{-61,-83,0},{-61,-82,0},{-60,-85,0},{-60,-111,0},{-59,-122,0},{-59,-115,0},{-60,-73,0},{-61,-76,0},{-61,-75,0},{-61,-74,0},{-61,-73,0},{-61,-72,0},{-59,-77,0},{-61,-70,0},{-61,-69,0},{-61,-68,0},{-59,-87,0},{-59,-85,0},{-53,-103,0}},{{-48,-127,0},{-48,-126,0},{-48,-125,0},{-48,-124,0},{-48,-123,0},{-48,-122,0},{-48,-121,0},{-48,-120,0},{-48,-119,0},{-48,-118,0},{-48,-117,0},{-48,-116,0},{-62,-83,0},{-48,-114,0},{-48,-113,0},{-48,-112,0},{-48,-111,0},{-48,-110,0},{-48,-109,0},{-48,-108,0},{-48,-107,0},{-48,-106,0},{-48,-105,0},{-48,-104,0},{-48,-103,0},{-48,-102,0},{-48,-101,0},{-48,-100,0},{-48,-99,0},{-48,-98,0},{-48,-97,0},{-48,-96,0},{-48,-95,0},{-48,-94,0},{-48,-93,0},{-48,-92,0},{-48,-91,0},{-48,-90,0},{-48,-89,0},{-48,-88,0},{-48,-87,0},{-48,-86,0},{-48,-85,0},{-48,-84,0},{-48,-83,0},{-48,-82,0},{-48,-81,0},{-48,-80,0},{-48,-79,0},{-48,-78,0},{-48,-77,0},{-48,-76,0},{-48,-75,0},{-48,-74,0},{-48,-73,0},{-48,-72,0},{-48,-71,0},{-48,-70,0},{-48,-69,0},{-48,-68,0},{-48,-67,0},{-48,-66,0},{-48,-65,0},{-47,-128,0},{-47,-127,0},{-47,-126,0},{-47,-125,0},{-47,-124,0},{-47,-123,0},{-47,-122,0},{-47,-121,0},{-47,-120,0},{-47,-119,0},{-47,-118,0},{-47,-117,0},{-47,-116,0},{-47,-115,0},{-47,-114,0},{-47,-113,0},{-30,-124,-106},{-47,-111,0},{-47,-110,0},{-47,-109,0},{-47,-108,0},{-47,-107,0},{-47,-106,0},{-47,-105,0},{-47,-104,0},{-47,-103,0},{-47,-102,0},{-47,-101,0},{-47,-100,0},{-62,-89,0},{-47,-98,0},{-47,-97,0}},{{-17,-97,-120},{-17,-97,-119},{-17,-97,-118},{-62,-92,0},{-17,-97,-117},{-17,-97,-116},{-17,-97,-115},{-17,-97,-114},{-17,-97,-113},{-17,-97,-112},{-17,-97,-111},{-40,-116,0},{-62,-83,0},{-17,-97,-110},{-17,-97,-109},{-17,-97,-108},{-17,-97,-107},{-17,-97,-106},{-17,-97,-105},{-17,-97,-104},{-17,-97,-103},{-17,-97,-102},{-17,-97,-101},{-17,-97,-100},{-17,-97,-99},{-17,-97,-98},{-40,-101,0},{-17,-97,-97},{-17,-97,-96},{-17,-97,-95},{-40,-97,0},{-17,-97,-94},{-40,-95,0},{-40,-94,0},{-40,-93,0},{-40,-92,0},{-40,-91,0},{-40,-90,0},{-40,-89,0},{-40,-88,0},{-40,-87,0},{-40,-86,0},{-40,-85,0},{-40,-84,0},{-40,-83,0},{-40,-82,0},{-40,-81,0},{-40,-80,0},{-40,-79,0},{-40,-78,0},{-40,-77,0},{-40,-76,0},{-40,-75,0},{-40,-74,0},{-40,-73,0},{-40,-72,0},{-40,-71,0},{-40,-70,0},{-17,-97,-93},{-17,-97,-92},{-17,-97,-91},{-17,-97,-90},{-17,-97,-89},{-39,-128,0},{-39,-127,0},{-39,-126,0},{-39,-125,0},{-39,-124,0},{-39,-123,0},{-39,-122,0},{-39,-121,0},{-39,-120,0},{-39,-119,0},{-39,-118,0},{-39,-117,0},{-39,-116,0},{-39,-115,0},{-39,-114,0},{-39,-113,0},{-39,-112,0},{-39,-111,0},{-39,-110,0},{-17,-97,-88},{-17,-97,-87},{-17,-97,-86},{-17,-97,-85},{-17,-97,-84},{-17,-97,-83},{-17,-97,-82},{-17,-97,-81},{-17,-97,-80},{-17,-97,-79},{-17,-97,-78},{-17,-97,-77},{-17,-97,-76}},{{-54,-67,0},{-54,-68,0},{-62,-93,0},{-17,-97,-126},{-17,-97,-125},{-62,-90,0},{-62,-89,0},{-62,-88,0},{-62,-87,0},{-17,-97,-124},{-62,-85,0},{-62,-84,0},{-62,-83,0},{-17,-97,-123},{-30,-128,-107},{-62,-80,0},{-62,-79,0},{-62,-78,0},{-62,-77,0},{-50,-124,0},{-50,-123,0},{-50,-122,0},{-62,-73,0},{-50,-120,0},{-50,-119,0},{-50,-118,0},{-62,-69,0},{-50,-116,0},{-62,-67,0},{-50,-114,0},{-50,-113,0},{-50,-112,0},{-50,-111,0},{-50,-110,0},{-50,-109,0},{-50,-108,0},{-50,-107,0},{-50,-106,0},{-50,-105,0},{-50,-104,0},{-50,-103,0},{-50,-102,0},{-50,-101,0},{-50,-100,0},{-50,-99,0},{-50,-98,0},{-50,-97,0},{-50,-96,0},{-50,-95,0},{-17,-97,-122},{-50,-93,0},{-50,-92,0},{-50,-91,0},{-50,-90,0},{-50,-89,0},{-50,-88,0},{-50,-87,0},{-50,-86,0},{-50,-85,0},{-50,-84,0},{-50,-83,0},{-50,-82,0},{-50,-81,0},{-50,-80,0},{-50,-79,0},{-50,-78,0},{-50,-77,0},{-50,-76,0},{-50,-75,0},{-50,-74,0},{-50,-73,0},{-50,-72,0},{-50,-71,0},{-50,-70,0},{-50,-69,0},{-50,-68,0},{-50,-67,0},{-50,-66,0},{-50,-65,0},{-49,-128,0},{-49,-127,0},{-49,-126,0},{-49,-125,0},{-49,-124,0},{-49,-123,0},{-49,-122,0},{-49,-121,0},{-49,-120,0},{-49,-119,0},{-49,-118,0},{-49,-117,0},{-49,-116,0},{-49,-115,0},{-49,-114,0},{-17,-97,-121}},{{-17,-98,-100},{-62,-94,0},{-62,-93,0},{-62,-92,0},{-62,-91,0},{-62,-90,0},{-62,-89,0},{-62,-88,0},{-62,-87,0},{-61,-105,0},{-62,-85,0},{-62,-84,0},{-62,-83,0},{-62,-82,0},{-30,-128,-66},{-62,-80,0},{-62,-79,0},{-62,-78,0},{-62,-77,0},{-62,-76,0},{-62,-75,0},{-62,-74,0},{-62,-73,0},{-62,-72,0},{-62,-71,0},{-61,-73,0},{-62,-69,0},{-62,-68,0},{-62,-67,0},{-62,-66,0},{-17,-98,-99},{-17,-98,-98},{-17,-98,-97},{-17,-98,-96},{-17,-98,-95},{-17,-98,-94},{-17,-98,-93},{-17,-98,-92},{-17,-98,-91},{-17,-98,-90},{-17,-98,-89},{-17,-98,-88},{-17,-98,-87},{-17,-98,-86},{-17,-98,-85},{-17,-98,-84},{-17,-98,-83},{-17,-98,-82},{-17,-98,-81},{-17,-98,-80},{-17,-98,-79},{-17,-98,-78},{-17,-98,-77},{-17,-98,-76},{-17,-98,-75},{-17,-98,-74},{-17,-98,-73},{-17,-98,-72},{-17,-98,-71},{-17,-98,-70},{-17,-98,-69},{-17,-98,-68},{-30,-128,-105},{-41,-112,0},{-41,-111,0},{-41,-110,0},{-41,-109,0},{-41,-108,0},{-41,-107,0},{-41,-106,0},{-41,-105,0},{-41,-104,0},{-41,-103,0},{-41,-102,0},{-41,-101,0},{-41,-100,0},{-41,-99,0},{-41,-98,0},{-41,-97,0},{-41,-96,0},{-41,-95,0},{-41,-94,0},{-41,-93,0},{-41,-92,0},{-41,-91,0},{-41,-90,0},{-41,-89,0},{-41,-88,0},{-41,-87,0},{-41,-86,0},{-17,-98,-67},{-17,-98,-66},{-17,-98,-65},{-17,-97,-128},{-17,-97,-127}},{{-62,-95,0},{-62,-94,0},{-62,-93,0},{-62,-92,0},{-62,-91,0},{-62,-90,0},{-62,-89,0},{-62,-88,0},{-62,-87,0},{-62,-86,0},{-62,-85,0},{-62,-84,0},{-62,-83,0},{-62,-82,0},{-62,-81,0},{-62,-80,0},{-62,-79,0},{-62,-78,0},{-62,-77,0},{-62,-76,0},{-62,-75,0},{-62,-74,0},{-62,-73,0},{-62,-72,0},{-62,-71,0},{-62,-70,0},{-62,-69,0},{-62,-68,0},{-62,-67,0},{-62,-66,0},{-62,-65,0},{-61,-128,0},{-61,-127,0},{-61,-126,0},{-61,-125,0},{-61,-124,0},{-61,-123,0},{-61,-122,0},{-61,-121,0},{-61,-120,0},{-61,-119,0},{-61,-118,0},{-61,-117,0},{-61,-116,0},{-61,-115,0},{-61,-114,0},{-61,-113,0},{-60,-98,0},{-61,-111,0},{-61,-110,0},{-61,-109,0},{-61,-108,0},{-61,-107,0},{-61,-106,0},{-61,-105,0},{-61,-104,0},{-61,-103,0},{-61,-102,0},{-61,-101,0},{-61,-100,0},{-60,-80,0},{-59,-98,0},{-61,-97,0},{-61,-96,0},{-61,-95,0},{-61,-94,0},{-61,-93,0},{-61,-92,0},{-61,-91,0},{-61,-90,0},{-61,-89,0},{-61,-88,0},{-61,-87,0},{-61,-86,0},{-61,-85,0},{-61,-84,0},{-61,-83,0},{-61,-82,0},{-61,-81,0},{-60,-97,0},{-61,-79,0},{-61,-78,0},{-61,-77,0},{-61,-76,0},{-61,-75,0},{-61,-74,0},{-61,-73,0},{-61,-72,0},{-61,-71,0},{-61,-70,0},{-61,-69,0},{-61,-68,0},{-60,-79,0},{-59,-97,0},{-61,-65,0}}};
            return table[codepage][symbol - 0x21];
        }

    private:
        const vector<char> & p21;
        vector<char> result;
        uint32_t index;
        bool error;
        unsigned char codepage;
    };

    string p21decode(const char * str) {
        vector<char> p21;
        string s = string(str);
        copy(s.begin(), s.end(), back_inserter(p21));
        p21decoder decoder(p21);
        if (decoder.has_error()) return "";
        auto result = decoder.unescape();
        return string(result.begin(), result.end());
    }

    vector<char> p21decode(vector<char> & str) {
        p21decoder decoder(str);
        if (decoder.has_error()) return {};
        return decoder.unescape();
    }

    bool need_to_decode(vector<char> & str) {
        for (uint32_t i = 0; i < str.size(); i++) {
            switch (str[i]) {
                case '\\':
                case '\'':
                    return true;
            }
        }
        return false;
    }
}
