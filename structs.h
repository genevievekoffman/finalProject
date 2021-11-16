//define all my structs in here 

#define BYTES 250 //since 250*4=1000 bytes
#define MAX_CHAR 50

typedef struct dummy_email {
    //unique id consists of server and sequence_num
    int server;
    int sequence_num; 
    //to & subject are part of the header
    char to[MAX_CHAR];
    char subject[MAX_CHAR];
    char message[BYTES]; 
}email;

typedef struct dummy_update {
    int type; //1 = new email, 2 = reading an email, 3 = deleting an email
    email email_; //null when type = 2 or 3
    //following will !null when type = 2 or 3
    int server;
    int sequence_num;
    char client[MAX_CHAR];
    
}update;
