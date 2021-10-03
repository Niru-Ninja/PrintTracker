/** VERSION NOTES:
*
* V0:
*
* PrintTracker is used to identify file formats that have lost their extension.
* The idea is to make a dynamic program that learns to identify the formats, exposing it to multiple files that are known are the format that you want to learn.
* It will have the commands:
* 1) -l: learn, the file you want to learn from is indicated. The extension is seen automatically.
* 2) -p: print, a print is generated from the previously generated "learn" files.
* 3) -i: identify, the indicated file is compared with the records to try to identify it.
*
* How it works: The idea is to make a simple program, and not a neural network. The program mainly uses components of the headers in the files to be identified, constant structures that are always repeated
* and they are easy to identify (we will not review the way the information is stored, or types of compression ... or other complex things.)
*
* (1) Learn: To learn, the file to learn will be indicated. When using this for the first time in a new extension:
* -> The program will generate two new files. These files will be an exact copy of the file that was mentioned in the previous command, one will be straight and the other will be inverted.
* When the user uses the command to learn again, in previously learned extensions:
* (The following is done once for the two generated files, for one the indicated file is read in normal way, for the other it is read backwards)
* -> The content of the generated file is compared with the new file indicated byte by byte.
* -> If the bytes are the same, they survive. If the bytes are different, they are removed from the generated file => All these surviving bytes are stored in the correct order in a "learn" file
* where the removed bytes become blank characters.
*
* (2) Print: The strings contained in the "learn" files are compiled in a "print" file together with the position of the initial byte and the length in bytes of the string.
* The position of the start byte of the string can be stored relative to the beginning of the file or relative to the end of the file. (Only one print file is generated).
* The following header will be used to identify a string of characters within the print file <Position | Length | Order> where the order indicates whether it is reading straight = d, or backwards = i.
*
* (3) Identify: To identify a file, the "print" file is read. It goes to the positions of the file indicated by it and the strings are compared. An answer will be given in percentages of similarity if
* multiple answers are possible.
*
* V1: improvements to make:
*
* PrintTracker must set a priority between the various traces found in the print files. It is not the same if the main header of the file matches, to a character string matching
*  in the middle.
* -> You have to be able to differentiate the main header in the print file from the other footprints, and give it a priority using the weights already established.
* -> Inform the user when it is believed that a header was found when trying to identify a file.
* You must also be able to identify character strings that can vary in position by a few bytes. (for example the EOF of pdf files, the way PrintTracker works in V0 cannot
* detect it).
* You must also be able to identify simple repeating patterns (again, as in the end of pdf files, there are very particular data structures).
* Character ratio: Use the ratio of the characters in the files as a footprint, if there is an extension that mostly uses one character over another, then this footprint will be useful.
* -> There can be multiple proportions, one general, one from the first third, another from the second third and another from the last third (or divide it into more sections, particularly the first third and the last).
* Many times the files vary in their structure in the last portion, this could be detected with this method. (If the file is very large the proportion is diluted).
*
* => Add the possibility of passing more than one file in the learn (-l) command so that PrintTracker learns a whole list of files sequentially.
* => Optimize the commands learn (-l) and identify (-i). (particularly learn this is taking a long time for large files).
**/

#include <stdio.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <bits/stdc++.h>
using namespace std;

//USER HELPING FUNCTION:
void printHelp(int errCode, string text)
{
    switch (errCode)
    {
    default:
        printf("\nPrintTracker is used to identify file formats that have lost their extension.");
        printf("\n\nIt has the commands: \n");
        printf("\n\t-l  : learn, the file you want to learn from is indicated. A file \"learn\" is generated\n\tUSE: printrack -l <file to learn>\n");
        printf("\n\t-p  : print, a print is generated from the previously generated \"learn\" files, only the file extension must be indicated to generate the \"print\" (Without the dot).\n\t USE: printrack -p <extension from the file>\n");
        //
        printf("\n\t-i  : identify, the indicated file is compared with the prints to try to identify it.\n\tUSE: printra -i <file to identify>\n");
        printf("\n");
        break;
    case 0:
        printf("\nERROR: There are too many parameters.\nFor information about the program use, use printrack -h\n\n");
        break;
    case 1:
        printf("\nERROR: The command \"%s\" is not known.\nFor information about the program use, use printrack -h\n\n", text.c_str());
        break;
    case 2:
        printf("\nERROR: File not found \"%s\".\n\n", text.c_str());
        break;
    case 3:
        printf("\nERROR: The file or the route to the file \"%s\" does not contain a file extension (Expecting a dot '.' in the name of the file)\n\n", text.c_str());
        break;
    case 4:
        printf("\nERROR: The extension \"%s\" was not learnt. (Must use the command -l with files of known extensions before using -p). The learn files of the requested extension could not be found.\n\n", text.c_str());
        break;
    case 5:
        printf("\nERROR: The file \"prints\" doesnt exist, to identify the format of a file you need the \"print\" files in said folder. Use the -h command for more information.\n\n");
        break;
    case 6:
        printf("\nERROR: The folder \"prints\" is empty, the \"print\" files are needed to identify the file format. Use the -h command for more information.\n\n");
        break;
    }
    return;
}

//STRUCTS:
struct guess
{
    string ext;
    unsigned int totalPrints;
    unsigned int detectedPrints;
    bool firstHeaderStrike;         // = true if the file to identify coincides in the first track with the print file.
    bool extensionInHeader;         // = true if a mention of the print file extension is found in the file to be identified, in the first 128 bytes.
    unsigned int extensionDistance; // Here we save the distance from the beginning of the file to the point where we find the extension in the header.
};

struct header
{
    int pos;
    int tam;
    char ori;
};

struct ans
{
    string ext;     // File extension.
    int weight;     // here we take into account the amount of total prints that the print file has, it is not the same 100% in a 1 print file than 100% in a 58 print file. It also takes into account the coincidences with the header.
    float pcent;    // Success rate, prints detected * 100 / total prints.
    int tp;         // Total prints, total amount of detected prints in the print file.
    bool gotHeader; // =true if we detect a header that matches the header in the file.
};

//UTILITY FUNCTIONS:
unsigned long long int filesize(FILE *archivo)
{
    unsigned long long int retMe = 0;
    fseek(archivo, 0, SEEK_END);
    retMe = ftell(archivo);
    return retMe;
}

//=0 exists, =-1 doesnt exists.
int checkFile(string checkMe)
{
    return (access(checkMe.c_str(), F_OK));
}

string extension(string filename)
{
    unsigned int longitud = filename.size();
    unsigned int aux = longitud;
    char lookingForDot;
    string retMe;

    lookingForDot = filename[aux];
    while (lookingForDot != '.' && aux > 0)
    {
        aux--;
        lookingForDot = filename[aux];
    }
    if (lookingForDot == '.')
    {
        retMe = filename.substr(aux + 1, longitud - aux);
    }
    else
        printHelp(3, filename);
    return retMe;
}

string nombre(string filename)
{
    string retMe = "";
    unsigned int longitud = filename.size();
    unsigned int aux = 0;
    while (filename[aux] != '.' && aux < longitud)
    {
        aux++;
    }
    if (filename[aux] == '.')
    {
        aux--;
        retMe = filename.substr(0, aux + 1);
    }
    return retMe;
}

string parseUntil(FILE *fileToParse, char endToken)
{
    string retMe;
    char byte;
    fread(&byte, 1, 1, fileToParse);
    while (byte != endToken && !feof(fileToParse))
    {
        retMe += byte;
        fread(&byte, 1, 1, fileToParse);
    }
    return retMe;
}

bool compareAnswers(const ans &a, const ans &b)
{
    return a.weight > b.weight;
}

//CORE FUNCTIONS:
void learn(string filename)
{
    char byte;  //Auxiliary, used to read the input file.
    char byte2; //Auxiliary, used to read output files.
    FILE *input = fopen(filename.c_str(), "rb");
    //Indices para leer los archivos cuando hay quilombo:
    unsigned long long int index = 1;
    unsigned long long int endIndex = filesize(input);

    //We check if the directory "learns" exists, if not we create it:
    if (checkFile("learns") == -1)
    {
        mkdir("learns");
    }
    //Routes for the "learn" files:
    string filepathDer = "learns\\" + extension(filename) + ".learn1";
    string filepathInv = "learns\\" + extension(filename) + ".learn2";
    //We check if we have to make the "learn" files or if they were created previously:
    if (checkFile(filepathDer) == -1 || checkFile(filepathInv) == -1)
    {                                                       //The extension is new and the files must be created:
        FILE *outputDer = fopen(filepathDer.c_str(), "wb"); //File reading straight.
        FILE *outputInv = fopen(filepathInv.c_str(), "wb"); //File reading reversed.
        //We copy the file exactly the same:
        //STRAIGHT:
        fseek(input, 0, SEEK_SET);
        fread(&byte, 1, 1, input);
        while (!feof(input))
        {
            fprintf(outputDer, "%c", byte);
            fread(&byte, 1, 1, input);
        }
        //INVERTED:
        while (index <= endIndex)
        {
            fseek(input, endIndex - index, SEEK_SET);
            fread(&byte, 1, 1, input);
            fprintf(outputInv, "%c", byte);
            index++;
        }
        fclose(outputDer);
        fclose(outputInv);
    }
    else
    {                                                        //We know the extension and the file must be modified:
        FILE *outputDer = fopen(filepathDer.c_str(), "rb+"); //File reading straight.
        FILE *outputInv = fopen(filepathInv.c_str(), "rb+"); //File reading reversed.
        //Modifying the files:
        //STRAIGHT:
        rewind(input);
        rewind(outputDer);
        while (!feof(outputDer) && !feof(input))
        {
            fread(&byte, 1, 1, input);
            fread(&byte2, 1, 1, outputDer);
            if (byte2 == ' ')
                continue;
            if (byte != byte2)
            {
                if (ftell(outputDer) != 0)
                    fseek(outputDer, -1L, SEEK_CUR);
                else
                    rewind(outputDer);
                fprintf(outputDer, " ");
                fseek(outputDer, 0L, SEEK_CUR);
            }
        }
        //INVERTED:
        rewind(input);
        rewind(outputInv);
        while (!feof(outputInv) && index <= endIndex)
        {
            fseek(input, endIndex - index, SEEK_SET);
            fread(&byte, 1, 1, input);
            fread(&byte2, 1, 1, outputInv);
            if (byte2 == ' ')
                continue;
            if (byte != byte2)
            {
                if (ftell(outputInv) != 0)
                    fseek(outputInv, -1L, SEEK_CUR);
                else
                    rewind(outputInv);
                fprintf(outputInv, " ");
                fseek(outputInv, 0L, SEEK_CUR);
            }
            index++;
        }
        fclose(outputDer);
        fclose(outputInv);
    }
    fclose(input);
    return;
}

void generatePrint(string ext)
{
    //We check if the "prints" directory exists, if not we create it:
    if (checkFile("prints") == -1)
    {
        mkdir("prints");
    }
    //We check if the extension that the user gave to us is already learnt:
    string learn1path = "learns/" + ext + ".learn1";
    string learn2path = "learns/" + ext + ".learn2";
    if (!(checkFile(learn1path) == 0 && checkFile(learn2path) == 0))
        printHelp(4, ext);
    else
    { //If the extension was learned:
        string printPath = "prints/" + ext + ".print";
        FILE *printFile = fopen(printPath.c_str(), "wb");
        FILE *learn1File = fopen(learn1path.c_str(), "rb");
        FILE *learn2File = fopen(learn2path.c_str(), "rb");

        char byte;
        unsigned long long int charChainIndex;
        vector<char> charChainPrint;
        //We read the files:
        //STRAIGHT:
        fread(&byte, 1, 1, learn1File);
        while (!feof(learn1File))
        {
            if (byte != ' ')
            {
                charChainIndex = ftell(learn1File) - 1;
                while (byte != ' ')
                {
                    charChainPrint.push_back(byte);
                    fread(&byte, 1, 1, learn1File);
                }
                if (charChainPrint.size() > 0)
                {
                    fprintf(printFile, "<%llu|%u|d>", charChainIndex, (unsigned)charChainPrint.size());
                    for (unsigned int i = 0; i < charChainPrint.size(); i++)
                    {
                        fprintf(printFile, "%c", charChainPrint[i]);
                    }
                }
                charChainPrint.clear();
            }
            else
                fread(&byte, 1, 1, learn1File);
        }
        fclose(learn1File);
        charChainPrint.clear();
        //INVERTED:
        fread(&byte, 1, 1, learn2File);
        while (!feof(learn2File))
        {
            if (byte != ' ')
            {
                charChainIndex = ftell(learn2File) - 1;
                while (byte != ' ')
                {
                    charChainPrint.push_back(byte);
                    fread(&byte, 1, 1, learn2File);
                }
                if (charChainPrint.size() > 0)
                {
                    fprintf(printFile, "<%llu|%u|i>", charChainIndex + (unsigned)charChainPrint.size(), (unsigned)charChainPrint.size());
                    for (unsigned int i = charChainPrint.size() - 1; i > 0; i--)
                    {
                        fprintf(printFile, "%c", charChainPrint[i]);
                    }
                    fprintf(printFile, "%c", charChainPrint[0]);
                }
                charChainPrint.clear();
            }
            else
                fread(&byte, 1, 1, learn2File);
        }
        fclose(learn2File);
        fclose(printFile);
    }
    return;
}

void identify(string filename)
{
    vector<guess> guesses;
    vector<string> prints;
    string printPath;
    unsigned int printIndex = 0;
    unsigned long long int fSize;
    guess insertor;
    insertor.ext.clear();
    insertor.detectedPrints = 0;
    insertor.totalPrints = 0;
    insertor.firstHeaderStrike = false;
    insertor.extensionInHeader = false;
    header headData;
    char byte;  //Byte that we read from the print files.
    char byte2; //Byte that we read from the file that the user gave to us (fileToIdentify).

    unsigned int fieldCharCounter = 0;
    unsigned int auxCounter = 0;
    bool isOnField = false; //This variable indicates if we are rading from a header or from the info between headers
    //It changes its value when we have read all the characters that the header indicated, and we wait for the start of another header on that place.

    //We identify, if they exist, all the files inside the "prints" folder:
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("prints")) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            prints.push_back(ent->d_name);
        }
        closedir(dir);
    }
    else
    {
        printHelp(5, "");
    }

    //We clean the vector of print files of the things we do not need and verify that it is not empty:
    prints.erase(prints.begin() + 0, prints.begin() + 2);
    if (prints.size() < 1)
        printHelp(6, "");

    //Open the file the user gave to us:
    FILE *fileToIdentify = fopen(filename.c_str(), "rb");
    fSize = filesize(fileToIdentify);

    //We go through the files in the "prints" folder one by one:
    while (printIndex < prints.size())
    {
        if (extension(prints[printIndex]) == "print")
        { //If the file has the extension we can read:
            printPath = "prints\\" + prints[printIndex];
            FILE *printFile = fopen(printPath.c_str(), "rb");
            insertor.ext = nombre(prints[printIndex]);
            //We parse the print file:
            fread(&byte, 1, 1, printFile);
            while (!feof(printFile))
            {
                //We extract the information from the mini-headers in the print file:
                if (byte == '<' && !isOnField && !feof(printFile))
                {
                    headData.pos = atoi(parseUntil(printFile, '|').c_str());
                    headData.tam = atoi(parseUntil(printFile, '|').c_str());
                    fread(&byte, 1, 1, printFile);
                    headData.ori = byte;
                    fread(&byte, 1, 1, printFile);
                    if (byte != '>')
                        break;
                    else
                    {
                        isOnField = true;
                        insertor.totalPrints++;
                    }
                }
                //We position the pointer in the file fileToIdentify, to the position indicated by the header of the print file:
                if (headData.ori == 'd')
                    fseek(fileToIdentify, headData.pos, SEEK_SET);
                else if (headData.ori == 'i')
                    fseek(fileToIdentify, fSize - headData.pos, SEEK_SET);
                else
                    break;
                //We read and compare between both files:
                if (isOnField)
                {
                    fread(&byte, 1, 1, printFile);
                    fread(&byte2, 1, 1, fileToIdentify);
                    while (fieldCharCounter <= headData.tam && !feof(printFile))
                    {
                        if (byte != byte2)
                            break;
                        fieldCharCounter++;
                        fread(&byte, 1, 1, printFile);
                        fread(&byte2, 1, 1, fileToIdentify);
                    }
                }
                //If we read everything then it means that they match 100% in that sector, if not we must continue until the next header:
                if (fieldCharCounter == headData.tam)
                {
                    insertor.detectedPrints++;
                    if (headData.pos == 0 && headData.ori == 'd')
                        insertor.firstHeaderStrike = true;
                }
                else
                {
                    auxCounter = fieldCharCounter;
                    while (auxCounter < headData.tam && !feof(printFile))
                    {
                        fread(&byte, 1, 1, printFile);
                        auxCounter++;
                    }
                }
                fieldCharCounter = 0;
                isOnField = false;
            }
            //We save everything that matched from the print file to the vector before closing it and moving on to the next:
            guesses.push_back(insertor);
            insertor.ext.clear();
            insertor.detectedPrints = 0;
            insertor.totalPrints = 0;
            insertor.firstHeaderStrike = false;
            insertor.extensionInHeader = false;
            fclose(printFile);
        }
        printIndex++;
    }
    //We give a result:
    vector<ans> answers;
    ans ansInsertor;
    for (int i = 0; i < guesses.size(); i++)
    {
        ansInsertor.ext = guesses[i].ext;
        ansInsertor.gotHeader = guesses[i].firstHeaderStrike;
        ansInsertor.pcent = guesses[i].detectedPrints * 100 / guesses[i].totalPrints;
        //if(ansInsertor.pcent > 100) ansInsertor.pcent = 100;
        ansInsertor.weight = ansInsertor.pcent * guesses[i].detectedPrints;
        if (ansInsertor.gotHeader)
            ansInsertor.weight *= 10;
        ansInsertor.tp = guesses[i].totalPrints;
        if (ansInsertor.pcent > 0)
            answers.push_back(ansInsertor);
    }
    sort(answers.begin(), answers.end(), compareAnswers);
    printf("\nResults: \n \n Extension | Success rate | Total prints \n");
    if (answers.size() <= 45)
    {
        for (int i = 0; i < answers.size(); i++)
        {
            if (answers[i].gotHeader)
                printf("  %10s| %22f| %15d   <= A header matching this extension was detected.\n", answers[i].ext.c_str(), answers[i].pcent, answers[i].tp);
            else
                printf("  %10s| %22f| %15d\n", answers[i].ext.c_str(), answers[i].pcent, answers[i].tp);
        }
    }
    else
    {
        int auxC;
        for (int i = 0; i < answers.size() && i <= 45; i++)
        {
            if (answers[i].gotHeader)
                printf("  %10s| %22f| %15d   <= A header matching this extension was detected.\n", answers[i].ext.c_str(), answers[i].pcent, answers[i].tp);
            else
                printf("  %10s| %22f| %15d\n", answers[i].ext.c_str(), answers[i].pcent, answers[i].tp);
            auxC = i;
        }
        printf(" %d Other possible extensions ...", answers.size() - auxC);
    }
    printf("\n\n");
    return;
}

int main(int argc, char *argv[])
{
    //We review the command that the user gives us:
    if (argc <= 2)
    {
        printHelp(-1, "");
        return 0;
    }
    if (argc == 3)
    {
        if (string(argv[1]) == "-l")
        {
            if (checkFile(argv[2]) == 0)
            {
                learn(string(argv[2]));
            }
            else
                printHelp(2, argv[2]);
        }
        else if (string(argv[1]) == "-p")
        {
            generatePrint(string(argv[2]));
        }
        else if (string(argv[1]) == "-i")
        {
            if (checkFile(argv[2]) == 0)
            {
                identify(string(argv[2]));
            }
            else
                printHelp(2, argv[2]);
        }
        else
        {
            printHelp(1, argv[1]);
        }
    }
    else
        printHelp(0, "");

    return 0;
}
