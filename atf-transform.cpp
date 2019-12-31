#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>

#include "3rdparty/jpegxr/jpegxr.h"
#include "3rdparty/jpegxr/jxr_priv.h"

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


bool decodeData( const unsigned char * src, int & index, int version, char * destination, int & output_size)
{
    int source_size;

    if( version == 3 )
    {
        source_size = ( src[ index ] << 24 ) + ( src[ index + 1 ] << 16 ) + ( src[ index + 2 ] << 8 ) + src[ index + 3 ];
        index += 4;
    }
    else
    {
        source_size = ( src[ index ] << 16 ) + ( src[ index + 1 ] << 8 ) + src[ index + 2 ];
        index += 3;
    }

    if( source_size == 0 )
    {
        return false;
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

    SizeT src_size = source_size;
    SizeT dest_size = output_size;
    auto res = LzmaDecode(reinterpret_cast<Byte*>(destination), &dest_size, reinterpret_cast<const Byte*>( src + index + 5 ), &src_size,
        reinterpret_cast<const Byte*>(src + index), 5, LZMA_FINISH_END, &status, &alloc, nullptr);

    if( res != SZ_OK || ( status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK && status != LZMA_STATUS_FINISHED_WITH_MARK ) )
    {
        return false;
    }

    output_size = dest_size;
    index += 5 + source_size;

    return true;
}

static char * globalDestination = nullptr;

bool decodeJpegXR(const unsigned char * src, int & index, int version, char * destination)
{
    int source_size;

    if( version == 3 )
    {
        source_size = ( src[ index ] << 24 ) + ( src[ index + 1 ] << 16 ) + ( src[ index + 2 ] << 8 ) + src[ index + 3 ];
        index += 4;
    }
    else
    {
        source_size = ( src[ index ] << 16 ) + ( src[ index + 1 ] << 8 ) + src[ index + 2 ];
        index += 3;
    }

    if( source_size == 0 )
    {
        return false;
    }

    auto container = jxr_create_container();
    auto image = jxr_create_input();

    jxr_set_user_data(image, destination);

    jxr_set_block_output(image,[](jxr_image_t image, int mx, int my, int*data){
        uint16_t * image_data = reinterpret_cast<uint16_t*>( jxr_get_user_data(image) );
        int32_t w = jxr_get_IMAGE_WIDTH(image);
        int32_t h = jxr_get_IMAGE_HEIGHT(image);
        int32_t n = jxr_get_IMAGE_CHANNELS(image);
        for ( int32_t y=0; y<16; y++) {
            int32_t dy = (my*16) + y;
            for ( int32_t x=0; x<16; x++) {
                int32_t dx = (mx*16) + x;
                if ( dy < h && dx < w ) {

                    int pixel_index = y*16 + x;

                    int r = data[ pixel_index * n + 2 ] & 0x1F;
                    int g = data[ pixel_index * n + 1 ] & 0x3F;
                    int b = data[ pixel_index * n + 0 ] & 0x1F;

                    image_data[ dy * w + dx ] =
                          (r << 11)
                        | (g <<  5)
                        |  b;
                }
            }
        }
    });
    auto result = jxr_read_image_container(container, reinterpret_cast<const unsigned char *>(src + index), source_size);
    auto image_offset = jxrc_image_offset(container, 0);
    auto image_size = jxrc_image_bytecount(container, 0);

    result = jxr_read_image_bitstream(image, reinterpret_cast<const unsigned char *>(src + index + image_offset), image_size);

    jxr_destroy( image );
    jxr_destroy_container( container );

    index += source_size;

    return true;
}

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

        int index;

        if( src[ 0 ] == 1 )
        {
            index += 5;
        }

        if( src[ index ] != 'A' || src[ index + 1 ] != 'T' || src[ index + 2 ] != 'F' )
        {
            std::cerr << "Invalid atf file";
            goto printusage;
        }

        int version = 0;

        if( src[ index + 6 ] == 255 )
        {
            index += 6;

            version = 3;
        }

        int size = ( src[ index + 3 ] << 16 ) + ( src[ index + 4 ] << 8 ) + src[ index + 5 ];

        ofile.put('A');
        ofile.put('T');
        ofile.put('F');
        //Placeholder
        ofile.put(uint8_t(0));
        ofile.put(uint8_t(0));
        ofile.put(uint8_t(0));

        int format = src[ index + 6 ];
        int cube = format >> 7;
        format &= 0x8F;

        if(cube)
        {
            std::cerr << "Cube is not supported yet" << std::endl;
            ofile.close();
            remove(ofilename);
            return -1;
        }

        if( format != ATF_FORMAT_COMPRESSED && format != ATF_FORMAT_COMPRESSEDALPHA )
        {
            std::cerr << "Not a compressed format, just copying" << std::endl;
            ofile.seekp(0, std::ios_base::beg);
            ofile.write(reinterpret_cast<char*>(src), filesize);
            return 0;
        }

        switch( format )
        {
            case ATF_FORMAT_COMPRESSED: ofile.put(uint8_t(ATF_FORMAT_COMPRESSEDRAW));break;
            case ATF_FORMAT_COMPRESSEDALPHA: ofile.put(uint8_t(ATF_FORMAT_COMPRESSEDRAWALPHA));break;
            default:
            {
                std::cerr << "Unsupported format(" << format << ")" << std::endl;
                ofile.close();
                remove(ofilename);
                return -1;
            }
        }

        int width = 1 << src[ index + 7 ];
        int height = 1 << src[ index + 8 ];
        int texture_count = src[ index + 9 ];

        ofile.put(src[index + 7]);
        ofile.put(src[index + 8]);
        ofile.put(src[index + 9]);

        index += 10;

        int texture_index = 0;

        int current_width = width;
        int current_height = height;

        auto * dest = new char[ current_width * current_height * 8 ];

        char * dest_alpha = nullptr;

        if( format == 4 )
        {
            dest_alpha = new char[ current_width * current_height * 4 ];
        }

        while(texture_index < texture_count)
        {
            /*
            if( (index + source_size) > (size + 6 ) )
            {
                std::cerr << "Error reading, wrong size" << std::endl;
                ofile.close();
                remove(ofilename);

                return -1;
            }
            */

            int skip_image_count;

            if( format == 2 )
            {
                int block_count = std::max(1,current_width/4)*std::max(1,current_height/4);

                int color_base_size = block_count * 2 * 2; //16 bit, 2 color per block
                int color_bit_size = block_count * 4; // 4 bytes of interpolation

                uint32_t * bits = reinterpret_cast<uint32_t*>( dest );

                uint16_t *cl0 = reinterpret_cast<uint16_t*>( bits + color_bit_size );
                uint16_t *cl1 = cl0 + (color_base_size / 4);

                int output_size = color_bit_size;
                bool result = decodeData(reinterpret_cast<unsigned char*>(src), index, version, reinterpret_cast<char*>(bits), output_size);

                if( output_size != color_bit_size )
                {
                    std::cerr << "Issue with bits" << std::endl;
                    result = false;
                }

                result |= decodeJpegXR(reinterpret_cast<unsigned char*>(src), index, version, reinterpret_cast<char*>(cl0) );

                if( result )
                {
                    output_size = std::max(2,current_width/4)*std::max(2,current_height/4) * 8;
                    ofile.put( uint8_t( output_size >> 16 ) );
                    ofile.put( uint8_t( output_size >> 8 ) );
                    ofile.put( uint8_t( output_size ) );

                    for( int i = 0; i < block_count; ++i )
                    {
                        ofile.write(reinterpret_cast<char*>(cl0 + i), 2);
                        ofile.write(reinterpret_cast<char*>(cl1 + i), 2);
                        ofile.write(reinterpret_cast<char*>(bits + i), 4);
                    }
                }
                else
                {
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                }

                skip_image_count = 6;
            }
            else if( format == 4 )
            {
                int block_count = std::max(1,current_width/4)*std::max(1,current_height/4);

                int alpha_base_size = block_count * 2; // 2 alpha byte (a0, a1)
                int alpha_bit_size = block_count * 6; // 3bit of interpolation per pixel
                int color_base_size = block_count * 2 * 2; //16 bit, 2 color per block
                int color_bit_size = block_count * 4; // 4 bytes of interpolation

                auto bits = reinterpret_cast<uint32_t*>( dest );
                auto alpha_bits = reinterpret_cast<uint8_t*>( dest_alpha );

                uint16_t *cl0 = reinterpret_cast<uint16_t*>( bits + color_bit_size );
                uint16_t *cl1 = cl0 + (color_base_size / 4);

                uint8_t *a0 = reinterpret_cast<uint8_t*>( alpha_bits + alpha_bit_size );
                uint8_t *a1 = a0 + alpha_base_size;

                //Alpha
                int output_size = alpha_bit_size;
                bool result = decodeData(reinterpret_cast<unsigned char*>(src), index, version, reinterpret_cast<char*>(alpha_bits), output_size);

                if( output_size != alpha_bit_size )
                {
                    std::cerr << "Issue with bits" << std::endl;
                    result = false;
                }

                result |= decodeJpegXR(reinterpret_cast<unsigned char*>(src), index, version, reinterpret_cast<char*>(a0) );

                //Color
                output_size = color_bit_size;
                result = decodeData(reinterpret_cast<unsigned char*>(src), index, version, reinterpret_cast<char*>(bits), output_size);

                if( output_size != color_bit_size )
                {
                    std::cerr << "Issue with bits" << std::endl;
                    result = false;
                }

                output_size = color_base_size;
                result |= decodeJpegXR(reinterpret_cast<unsigned char*>(src), index, version, reinterpret_cast<char*>(cl0) );

                if( output_size != color_base_size )
                {
                    std::cerr << "Issue with color" << std::endl;
                    result = false;
                }

                if( result )
                {
                    output_size = std::max(2,current_width/4)*std::max(2,current_height/4) * 16;
                    ofile.put( uint8_t( output_size >> 16 ) );
                    ofile.put( uint8_t( output_size >> 8 ) );
                    ofile.put( uint8_t( output_size ) );

                    for( int i = 0; i < block_count; ++i )
                    {
                        ofile.put(a0[i]);
                        ofile.put(a1[i]);
                        ofile.write(reinterpret_cast<char*>( &alpha_bits[ i * 6 ] ), 6 );
                        ofile.write(reinterpret_cast<char*>(cl0 + i), 2);
                        ofile.write(reinterpret_cast<char*>(cl1 + i), 2);
                        ofile.write(reinterpret_cast<char*>(bits + i), 4);
                    }
                }
                else
                {
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                    ofile.put(uint8_t(0));
                }

                skip_image_count = 12;
            }

            for( int i = 0; i< skip_image_count; ++i) //Skip other format (only dxt1)
            {
                int source_size_2;
                if( version == 3 )
                {
                    source_size_2 = ( src[ index ] << 24 ) + ( src[ index + 1 ] << 16 ) + ( src[ index + 2 ] << 8 ) + src[ index + 3 ];
                    index += 4 + source_size_2;
                }
                else
                {
                    source_size_2 = ( src[ index ] << 16 ) + ( src[ index + 1 ] << 8 ) + src[ index + 2 ];
                    index += 3 + source_size_2;
                }


                ofile.put(uint8_t(0));
                ofile.put(uint8_t(0));
                ofile.put(uint8_t(0));
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