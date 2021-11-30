#define MAX_MESSLEN 250 //since 250*4=1000 bytes
#define MAX_SERVERS 5
#define MAX_USERNAME 20
#define MAX_CELLS 20

typedef struct dummy_email {
    char to[MAX_USERNAME];
    char subject[MAX_USERNAME];
    char message[MAX_MESSLEN]; 
    char sender[MAX_USERNAME];
}email;

typedef struct dummy_id {
    int server;
    int sequence_num;
}id;

typedef struct dummy_update {
    int type; //1 = new email, 2 = reading an email, 3 = deleting an email
    email email_; //null when type = 2 or 3
        //when type = 2 or 3 -> email_.to is the client that made the request! (open their file) 
    //following will !null when type = 2 or 3
    id mail_id; 
}update;

typedef struct dummy_cell {
    int sn; //serial number 1-20
    char status; //'r', 'u', 'd'
    email mail; 
    id mail_id;
}cell;

typedef struct dummy_window {
    cell window[MAX_CELLS]; 
}window;

typedef struct dummy_node{
    struct dummy_update *update; 
    struct dummy_node *nxt; 
}node;

typedef struct dummy_linkedList{
    struct dummy_node *sentinel;
}linkedList;
