//server program

#include "structs.h"
#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_VSSETS 10 //?what is this for
#define MAX_LEN 256 //max line in file

static int server_index; //unique server index
static char Private_group[MAX_GROUP_NAME];
static char Server[80];
static mailbox Mbox;
static char Spread_name[80];
static int To_exit = 0;
static char sc;

static void Read_message();
static void Bye();
static void request_mailbox(char *client);
static void updates_window_init();
static int repo_insert(linkedList *l, update* u);
//void print_repo(linkedList *l);
void print_window(window* w);
static int check_status(id *id_, char *file);

static void get_filename(char *fn, char *client, int r);
static update *new_update;
static id *unique_id;
static int updates_sent; //sequence num
FILE *fw;

//an array of pointers to Linked lists
static linkedList* updates_window[MAX_SERVERS]; 
static id* status_matrix[MAX_SERVERS][MAX_SERVERS]; //5x5 matrix with pointers to id's

int main(int argc, char **argv)
{
    sp_time test_timeout;
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    updates_sent = 0; 
    new_update = malloc(sizeof(update));
    unique_id = malloc(sizeof(id));

    if ( argc != 2 ) {
        printf("Usage: server <1-5>\n");
        exit(0);
    }

    server_index = atoi(argv[1]); 
    sprintf( Server, "server" );
    sprintf( Spread_name, "10050"); //always connects to my port
    strncat( Server, argv[1], 1 );
    sprintf( Private_group, argv[1] );
    
    /* connect to SPREAD */ 
    int ret;
    ret = SP_connect_timeout( Spread_name, Server, 0, 1, &Mbox, Private_group, test_timeout ); 
    if( ret != ACCEPT_SESSION ) {
        SP_error( ret );
        Bye();
    }
    printf("\n\t>Server: connected to %s with private group %s\n", Spread_name, Private_group );
    
    //joins its own public group
    ret = SP_join( Mbox, Server );
    if ( ret < 0 ) SP_error( ret );
    
    //joins the network group with all servers in it
    ret = SP_join( Mbox, "all_servers");
    if ( ret < 0 ) SP_error( ret );

    updates_window_init(); 
    //print_repo(updates_window[0]);
    
    E_init();
    E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY ); 
    E_handle_events();
}

static void Read_message()
{
    static  char    mess[sizeof(update)]; 
    char		    sender[MAX_GROUP_NAME];
    char            target_groups[MAX_SERVERS][MAX_GROUP_NAME];
    membership_info memb_info;
    int             service_type;
    int16           mess_type;
    int             endian_mismatch;
    int             num_groups;
    int             ret;
    sc = (server_index + '0'); 

    ret = SP_receive( Mbox, &service_type, sender, 10, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);

    
    if ( Is_regular_mess( service_type ) )
    {
        printf("\nregular msg\n");
        mess[ret] = 0;
        switch ( mess_type )
        {
            case 0: ; //client connection request 
                //joins the group server_client
                char *msg = (char*)mess;
                char sc_group[MAX_MESSLEN];
                sprintf(sc_group, &sc);
                strcat(sc_group, msg);
                int ret = SP_join(Mbox, sc_group);
                if ( ret < 0 ) SP_error( ret ); 
                break;
                
            case 1: ; //new email from a client  
                //creates a new update
                updates_sent++;
                new_update->sequence_num = updates_sent;
                new_update->type = 1; //new email
                new_update->mail_id.server = server_index;
                new_update->mail_id.sequence_num = updates_sent;
               
                email *test_email = (email*)mess;
                new_update->email_ = *test_email;
                
                //write the new email to our log/file for that users email file
                char filename[MAX_USERNAME+11] = "";
                get_filename(filename, new_update->email_.to, 0); //0=emails.txt
                if ( ( fw = fopen(filename, "a") ) == NULL ) {
                    perror("fopen");
                    exit(0);
                }
                ret = fprintf(fw, "%d %d|%s|%s|%s|%s\n", new_update->mail_id.server, new_update->mail_id.sequence_num, new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender); //server seq_num|to|subject|msg|sender
                if ( ret < 0 )
                    printf("fprintf error");
                fclose(fw);
                
                //apply the update: adds new_update to our linked list in updates_window
                ret = repo_insert(updates_window[server_index-1], new_update);
                if (ret == -1) //error
                    printf("error");
                //print_repo(updates_window[server_index-1]);
                
                //update our status_matrix to our seq_num,server_index (unique_id)
                status_matrix[server_index-1][server_index-1] = unique_id;
                
                //multicast the update to all_servers group
                ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 5, sizeof(update), (char*)(new_update));
                break;
        
            case 2: ; //received a read request from client
                printf("\ncase 2: received read request\n");
                //mess will be a request type 
                updates_sent++;
                new_update->sequence_num = updates_sent; //it would be server, seq num
                new_update->type = 2; //read an email request
                
                request *temp_req = (request*)mess;
                new_update->mail_id = temp_req->mail_id;

                strcpy(new_update->email_.to, temp_req->user);
                printf("\ncells to = %s",new_update->email_.to);
                printf("\ncells unique_id = <%d,%d>\n",new_update->mail_id.server,new_update->mail_id.sequence_num);
                
                //write to our log
                
                char fn[MAX_USERNAME+11] = "";
                get_filename(fn, new_update->email_.to, 1); //1=reads.txt
                FILE* fw2;
                if ( ( fw2 = fopen(fn, "a") ) == NULL ) {
                    perror("fopen");
                    exit(0);
                }

                //write the emails id in here
                ret = fprintf(fw2, "%d %d\n", new_update->mail_id.server, new_update->mail_id.sequence_num);
                if ( ret < 0 )
                    printf("fprintf error");
                fclose(fw2);

                /*
                char fn[MAX_USERNAME+11]; //maxusername + 1(serverindex) + 10(emails.txt) = + 11
                sprintf(fn, &sc);
                strcat(fn, new_update->email_.to);
                printf("\n\topening %semails.txt\n", fn);
                FILE* fwr;
                if ( (fwr = fopen( ( strcat(fn, endtxt) ) , "r+") ) == NULL ) { //opens to read & write
                    perror("fopen");
                    exit(0);
                }
                */
                /* my attempt to change the 'u' to 'r' in a file
                //get line by line
                //read the first char of each line (id) 
                //if the id matches, then this is the line we will edit!

                char buff[256]; //reads everyline into buff 
                FILE* behind_fr = fopen(fn, "r+"); 
                behind_fr = fwr; //one line behind
                while ( fgets(buff, 256, fwr) )
                {
                    printf("\n>%s",buff);
                    printf("\nbuff[0] = %c",buff[0]);
                    printf("temp_req->mail_id.server = %d", temp_req->mail_id.server);
                   
                    if ( atoi(&buff[0]) == temp_req->mail_id.server ) {
                        char tkn[] = "|"; 
                        char *extract = strtok(&buff[2], tkn); //buff[2] starts the sequence_num
                        printf("extract = %s\n",extract);
                        if(atoi(extract) == temp_req->mail_id.sequence_num) //found the email
                        {
                            printf("buff now = %s\n", buff);
                            //move behind_fr to the last char of that line
                            char c;
                            do {
                                c = (char)fgetc(behind_fr);
                                printf("%c",c);
                            } while(c != EOF);
                       } 
                    }
                    behind_fr = fwr;
                }
                */
                //go to the last char and change it to a 'r'
                //apply the update
                
                break;

            case 4: ;
                //received a mailbox request from client
                char *client = (char*)mess;
                request_mailbox(client);
                break;

            case 5: ;
                printf("\ncase 5: received an udpate from server on all servers group");
                //ignore update if it's from ourself
                if ( server_index == sender[7] ) break;
                
                //make sure we can print the update here:

                update *new_update= (update*)mess;
                printf("\nupdate type = %d", new_update->type);
                printf("\nupdates_id = <%d,%d>", new_update->mail_id.server, new_update->mail_id.sequence_num);
                printf("\nemail: \n\tto: %s \n\tsubject: %s \n\tmessage: %s \n\tfrom: %s", new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender);
                //First: save this update to the servers log file
                
                //check the type of the update
                //if it's a new email -> save the emails info to its log file
                //if it's a read or delete -> save the unique id to corelating file

                break;

            default:
                printf("defualt unknown");
                break;
        }

    } else if (Is_membership_mess( service_type ) )
    {
        printf("\nmembership msg\n");
        ret = SP_get_memb_info( mess, service_type, &memb_info );
        if (ret < 0)
        {
            printf("BUG: membership message does not have valid body\n");
            SP_error( ret );
            exit(1);
        }
        if ( Is_reg_memb_mess( service_type ) )
        {
            printf("Received REGULAR membership for group %s with %d members, where I am member %d:\n",
                sender, num_groups, mess_type );
            for( int i=0; i < num_groups; i++ )
                printf("\t%s\n", &target_groups[i][0] );
            printf("grp id is %d %d %d\n",memb_info.gid.id[0], memb_info.gid.id[1], memb_info.gid.id[2] ); 

            if ( Is_caused_join_mess( service_type ) ){
                printf("Due to the JOIN of %s\n", memb_info.changed_member );
            } else if( Is_caused_leave_mess( service_type ) ){
                //go into state transfer & above
                printf("Due to the LEAVE of %s\n", memb_info.changed_member );
            } else if( Is_caused_disconnect_mess( service_type ) ){
                printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
            } else if( Is_caused_network_mess( service_type ) )
            {
            }
        
        }else if (Is_transition_mess(   service_type ) ) {
            printf("received TRANSITIONAL membership for group %s\n", sender );
        }else if( Is_caused_leave_mess( service_type ) ){
            printf("received membership message that left group %s\n", sender );
        }else printf("received incorrecty membership message of type 0x%x\n", service_type );
    
    } else printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);

}

/* creates a new window filled with cells and sends it to the client */
static void request_mailbox(char *client)
{
    window new_window;
    memset(&new_window, 0, sizeof(window)); //fill it with 0's
    int sn = 1; 
    FILE *fr; //pointer to file for reading 
    sc = (server_index + '0'); 
   
    char filename[MAX_USERNAME+11] = "";
    get_filename(filename, client, 0); //0 is for emails

    //open their emails file 
    if ( (fr = fopen(filename , "r") ) == NULL ) {  //done reading emails from file or no emails
        int ret = SP_multicast( Mbox, AGREED_MESS, client, 0, sizeof(window), (char*)&new_window );
        if (ret < 0 ) SP_error( ret );
        return;
    }
    cell *new_cell;
       
    /* fills the email & id within this cell */
    char buff[256]; //reads everyline into buff 
    while ( sn <= 20 && fgets(buff, 256, fr))
    {
        new_cell = &new_window.window[sn-1];
        new_cell->sn = sn;

        //gets rid of the new line
        buff[strcspn(buff,"\n")] = 0;
        char tkn[] = "|"; 
        char *extract = strtok(buff, tkn);
        int round = 0;
        while(extract != NULL)
        {
            switch(round) {
                case 0: ;
                    new_cell->mail_id.server = atoi(&extract[0]);
                    new_cell->mail_id.sequence_num = atoi(&extract[2]);
                    break;
                case 2: ;
                    printf("\t extract = %s\n", extract);
                    sprintf(new_cell->mail.subject,extract);
                    break;
                case 3: ;
                    sprintf(new_cell->mail.message,extract);
                    break;
                case 4: ;
                    sprintf(new_cell->mail.sender,extract);
                    break;
            }
            extract = strtok(NULL, tkn);
            round++;
        }
        sprintf(new_cell->mail.to,client);

        //check the status of the id
        
        //get_filename(temp_fn, client, 2); //2 is for deletes
        //^^CAUSING ISSUES I THINK ITS WRITING OVER DATA IN SOME BUFFER...

        char temp_fn[MAX_USERNAME+11] = "";
        sc = (server_index + '0'); 
        char temp[] = "deletes.txt";
        char temp2[] = "reads.txt";
        strcat(temp_fn, &sc);
        strcat(temp_fn, client);
       
        strcat(temp_fn, temp);
        printf("\ntemp_fn = %s\n",temp_fn);
        char stat = 'u';
        if ( check_status(&new_cell->mail_id, temp_fn) == 1 ) { //email has been deleted
            stat = 'd';
        } else { //check if it's read
            //get_filename(temp_fn, client, 1);//1 is for reads
            memset(temp_fn, '\0', sizeof(temp_fn));
            strcat(temp_fn, &sc);
            strcat(temp_fn, client);
            strcat(temp_fn, temp2);
            printf("\ntemp_fn = %s\n",temp_fn);
            if ( check_status(&new_cell->mail_id, temp_fn) == 1 )
                stat = 'r';
        }
        sprintf(&new_cell->status, &stat);
        sn++;
    }
    //TODO: WE NEED A CHECK TO LOOK AT THE DELETE & READ FILES AND UPDATE THE CELLS STATUS
    //send the new_window to the client
    print_window(&new_window);
    int ret = SP_multicast(Mbox, AGREED_MESS, client, 0, sizeof(window), (char*)&new_window);
    if ( ret < 0 ) SP_error( ret ); 
}

/* init the updates_window: array of 5 linked lists */
static void updates_window_init()
{
    //TODO: create a func to free all sentinel nodes before ending program
    printf("\ninit update_window");

    for (int i = 0; i < MAX_SERVERS; i++) {
        linkedList *new_list;
        new_list = malloc(sizeof(linkedList));
        
        node *sentinel;
        sentinel = malloc(sizeof(node));
        sentinel->update = NULL;
        sentinel->nxt = NULL;
        
        new_list->sentinel = sentinel;
        updates_window[i] = new_list; 
    }
}

//returns -1 if something went wrong while inserting new node/update
int repo_insert(linkedList *l, update* u)
{
    node *pnode;
    pnode = malloc(sizeof(node));
    if( pnode == NULL ) return -1;

    pnode->update = u;
    pnode->nxt = l->sentinel->nxt;
    l->sentinel->nxt = pnode;
    return 0;
}

//prints the updates in the linked list
/*
void print_repo(linkedList *l)
{
    if(l->sentinel->nxt == NULL) return;
    node *ptemp = l->sentinel;
    do {
        ptemp = ptemp->nxt;
        printf("\nseq_num=%d, server=%d", ptemp->update->mail_id->sequence_num, ptemp->update->id->server);
    } while(ptemp->nxt != NULL);

}
*/

//prints contents of the window
void print_window(window* w)
{
    cell *cell_;
    printf("\tsn#\t<server,seq_num>\tsubject\tmessage\tfrom\n");
    for ( int i = 0; i < MAX_CELLS; i++ ) {
        cell_ = &w->window[i];
        printf("\n\t%d\t<%d,%d>\t%s\t%s\t%s\t%c", cell_->sn, cell_->mail_id.server, cell_->mail_id.sequence_num, cell_->mail.subject, cell_->mail.message, cell_->mail.sender, cell_->status);
    }

}

/* Creates the correct filename to open 
 * fn is a pointer to an empty filename that this func will update */
static void get_filename(char fn[MAX_USERNAME+11], char *client, int r)
{
    memset(fn, '\0', MAX_USERNAME+11);
    //r = 0 (emails.txt) 1 (reads.txt) 2 (deletes.txt)
    sc = (server_index + '0'); 
    char *tmp = (r == 0) ? "emails.txt" : ( (r == 1) ? "reads.txt" : "deletes.txt" );
    strcat(fn, &sc);
    strcat(fn, client);
    strcat(fn, tmp);
    return;
}

/* Returns 1 if the id_ exists in the file else 0*/
static int check_status(id *id_, char *file)
{
    FILE *fr2;
    printf("file opening = %s", file);
    if ( (fr2 = fopen( file , "r") ) == NULL )
        return 0; //nothing to open/no emails deleted or read
    //otherwise iterate through and look for mail_id
    char buff2[256]; //reads everyline into buff
    while ( fgets(buff2, 256, fr2) )
    {
        if ( atoi(&buff2[0]) == id_->server && atoi(&buff2[2]) == id_->sequence_num ) //same id
            return 1;
    }
    fclose(fr2);

    return 0;
}

static void Bye()
{
    To_exit = 1;
    free(new_update);
    free(unique_id);
    printf("\nBye.\n");
    //E_detach_fd( Mbox, READ_FD ); 
    SP_disconnect( Mbox );
    exit(0);
}
