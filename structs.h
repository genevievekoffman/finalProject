//define all my structs in here 

#define BYTES 20 
#define MAX_MESSLEN 250 //since 250*4=1000 bytes
#define MAX_SERVERS 5
#define MAX_USERNAME 20

typedef struct dummy_email {
    char to[MAX_USERNAME];
    char subject[BYTES];
    char message[MAX_MESSLEN]; 
    char sender[MAX_USERNAME];
}email;

typedef struct dummy_update {
    int type; //1 = new email, 2 = reading an email, 3 = deleting an email
    email email_; //null when type = 2 or 3
    //following will !null when type = 2 or 3
    int server;
    int sequence_num;
}update;

typedef struct dummy_cell {
    int sn; //serial number 1-20
    char status; //'r', 'u', 'd'

    //pointer to an email
    email* contents;

    //unique id consists of server and sequence_num
    int server_index;
    int sequence_num;
}cell;
