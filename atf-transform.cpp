#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>


extern "C"
{
#include "3rdparty/lzma/LzmaLib.h"
#include "3rdparty/lzma/LzmaDec.h"
}

void print_usage()
{
    std::cout << R"(atf-transform V0.1

Usage: atf-transform -i input.atf -o output.atf

Convert atf lzma encoded to raw representation. Also remove the jpg-xr version
)";
}

enum {
    ATF_FORMAT_888				 = 0x00,
    ATF_FORMAT_8888				 = 0x01,
    ATF_FORMAT_COMPRESSED		 = 0x02,
    ATF_FORMAT_COMPRESSEDRAW	 = 0x03,
    ATF_FORMAT_COMPRESSEDALPHA	 = 0x04,
    ATF_FORMAT_COMPRESSEDRAWALPHA= 0x05,
    ATF_FORMAT_LAST				 = 0x05,
    ATF_FORMAT_CUBEMAP			 = 0x80,
};

static std::ifstream ifile;
static std::ofstream ofile;

static const char * ifilename;
static const char * ofilename;

int main(int argc, char *argv[]) {

    if ( argc > 1) {
        for (int32_t c = 1; c < argc; c++) {
            if (argv[c][0] == '-') {
                if (argv[c][1] == 'i') {
                    ifile.open(argv[c+1],std::ios::in|std::ios::binary);
                    if ( !ifile.is_open() ) {
                        std::cerr << "Could not open input file. '";
                        std::cerr << argv[c+1];
                        std::cerr << "'\n\n";
                        return -1;
                    }
                    ifilename = argv[c+1];
                } else if (argv[c][1] == 'o') {
                    if ( argc < c+1 ) {
                        std::cerr << "Missing output file name.\n\n";
                        return -1;
                    }
                    ofile.open(argv[c+1],std::ios::out|std::ios::binary);
                    if ( !ofile.is_open() ) {
                        std::cerr << "Could not open output file. '";
                        std::cerr << argv[c+1];
                        std::cerr << "'\n\n";
                        return -1;
                    }
                    ofilename = argv[c+1];
                }
            }
        }

        if ( !ifile.is_open() ) {
            std::cerr << "No input file provided.\n";
            goto printusage;
        }

        if ( !ofile.is_open() ) {
            std::cerr << "No output file provided.\n";
            goto printusage;
        }

        std::cout << "Converting " << ifilename << " to " << ofilename << std::endl;

        ifile.seekg(0,std::ios_base::end);
        size_t filesize = ifile.tellg();
        ifile.seekg(0,std::ios_base::beg);

        uint8_t *src = new uint8_t [filesize];
        ifile.read((char *)src,filesize);

        if( src[ 0 ] != 'A' || src[ 1 ] != 'T' || src[ 2 ] != 'F' )
        {
            std::cerr << "Invalid atf file";
            goto printusage;
        }

        int size = ( src[ 3 ] << 16 ) + ( src[ 4 ] << 8 ) + src[ 5 ];

        ofile.put('A');
        ofile.put('T');
        ofile.put('F');
        //Placeholder
        ofile.put(uint8_t(0));
        ofile.put(uint8_t(0));
        ofile.put(uint8_t(0));

        int format = src[ 6 ];

        if( format == 255 )
        {
            std::cerr << "New atf format, not supported yet";
            goto printusage;
        }

        int cube = format >> 7;
        format &= 0x8F;

        if(cube)
        {
            std::cerr << "Cube is not supported yet" << std::endl;
            return -1;
        }

        if( format != ATF_FORMAT_COMPRESSED && format != ATF_FORMAT_COMPRESSEDALPHA )
        {
            std::cerr << "Only compressed and alpha compressed are supported" << std::endl;
            return -1;
        }

        switch( format )
        {
            case ATF_FORMAT_COMPRESSED: ofile.put(uint8_t(ATF_FORMAT_COMPRESSEDRAW));break;
            case ATF_FORMAT_COMPRESSEDALPHA: ofile.put(uint8_t(ATF_FORMAT_COMPRESSEDRAWALPHA));break;
            default: std::cerr << "Unsupported format" << std::endl; return -1;
        }

        int width = 1 << src[ 7 ];
        int height = 1 << src[ 8 ];
        int texture_count = src[ 9 ];

        ofile.put(src[7]);
        ofile.put(src[8]);
        ofile.put(src[9]);

        int index = 10;

        int texture_index = 0;

        int current_width = width;
        int current_height = height;

        auto dest = new Byte[ current_width * current_height ];

        while(texture_index < texture_count)
        {
            int source_size = ( src[ index ] << 16 ) + ( src[ index + 1 ] << 8 ) + src[ index + 2 ];

            index += 3;

            if( source_size != 0 )
            {
                if( (index + source_size) > (size + 6 ) )
                {
                    std::cerr << "Error reading, wrong size" << std::endl;

                    return -1;
                }

                auto _malloc = [](void *, size_t s)
                {
                    return malloc(s);
                };

                auto _free = [](void*, void *p)
                {
                    free(p);
                };

                ISzAlloc alloc{ _malloc, _free };
                source_size -=5;
                ELzmaStatus status;
                SizeT expected_size;

                if( format == 2 )
                {
                    expected_size = std::max(1,current_width/4)*std::max(1,current_height/4)*4;
                }
                else if( format == 4 )
                {
                    expected_size = std::max(1,current_width/4)*std::max(1,current_height/4)*6;
                }

                SizeT src_size = source_size;
                SizeT dest_size = expected_size;
                auto res = LzmaDecode(dest, &dest_size, src + index + 5, &src_size,
                    src + index , 5, LZMA_FINISH_END, &status, &alloc, nullptr);

                if( res != SZ_OK || ( status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK && status != LZMA_STATUS_FINISHED_WITH_MARK ) )
                {
                    std::cerr << "Invalid LZMA buffer (" << res << ", " << status << ")" << std::endl;
                    return -1;
                }

                ofile.put( uint8_t( expected_size >> 16 ) );
                ofile.put( uint8_t( expected_size >> 8 ) );
                ofile.put( uint8_t( expected_size ) );
                ofile.write( reinterpret_cast<char*>(dest), expected_size );

                index += 5 + source_size;
            }
            else
            {
                ofile.put( 0 );
                ofile.put( 0 );
                ofile.put( 0 );
            }

            for( int i = 0; i< 7; ++i) //Skip other format (only dxt1)
            {
                int source_size_2 = ( src[ index ] << 16 ) + ( src[ index + 1 ] << 8 ) + src[ index + 2 ];

                index += 3 + source_size_2;

                ofile.put( 0 );
                ofile.put( 0 );
                ofile.put( 0 );
            }

            ++texture_index;
            current_width >>= 1;
            current_height >>= 1;
        }

        delete[] dest;

        auto total_size = ofile.tellp();

        ofile.seekp( 3, std::ios_base::beg );
        ofile.put( uint8_t( total_size >> 16 ) );
        ofile.put( uint8_t( total_size >> 8 ) );
        ofile.put( uint8_t( total_size ) );

        std::cout << "Conversion succeeded" << std::endl;
        return 0;
    }
printusage:
    print_usage();
    return -1;
}