#include <iostream>
#include "videoplayer.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <video_file>\n";
        return -1;
    }

    try
    {
        VideoPlayer player(argv[1]);
        player.open();
        player.start();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}