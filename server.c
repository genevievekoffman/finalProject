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
    //vs_set_info     vssets[MAX_VSSETS];
    //int             num_vs_sets;
    //unsigned int    my_vsset_index;
    //char            members[MAX_SERVERS][MAX_GROUP_NAME];
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
                new_update->type = 1; //new email
                new_update->mail_id.server = server_index;
                new_update->mail_id.sequence_num = updates_sent;
               
                email *test_email = (email*)mess;
                new_update->email_ = *test_email;
                //strcpy(*new_update->email_, (email*)mess);
                
                printf("\nnew_update->email_.to = %s", new_update->email_.to);
                printf("\nnew_update->email_.subject = %s", new_update->email_.subject);
                printf("new_update->email_.message = %s", new_update->email_.message);
                printf("new_update->email_.sender = %s\n", new_update->email_.sender);

                //write the new email to our log/file for that users email file
                //char filename[] = "/tmp/ts_";
                char filename[MAX_USERNAME+11]; //maxusername + 1(serverindex) + 10(emails.txt) = + 11
                sprintf(filename, &sc);
                strcat(filename, new_update->email_.to);
                char endtxt[11];
                strcpy(endtxt,"emails.txt");
                if ( (fw = fopen( ( strcat(filename, endtxt) ) , "a") ) == NULL ) { //a appends instead of overwriting the file
                    perror("fopen");
                    exit(0);
                }
                ret = fprintf(fw, "%d %d|%s|%s|%s|%s\n", new_update->mail_id.server, new_update->mail_id.sequence_num, new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender); //server seq_num|to|subject|msg|sender
                if ( ret < 0 )
                    printf("fprintf error");
                fclose(fw);
                
                //TODO: apply the update
                //adds new_update to our linked list in updates_window
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
                //mess will be a cell!
                break;

            case 4: ;
                //received a mailbox request from client
                char *client = (char*)mess;
                request_mailbox(client);
                break;

            case 5: ;
                printf("\ncase 5: received an udpate from server on all servers group");
                //ignore update if it's from ourself
                //sender[7] is the index ~ #server1#ugrad8
                //if ( server_index == sender[7] ) break;
                
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
    
    //char filename[] = "/tmp/ts_";
    char file[MAX_USERNAME+11] = ""; // maxusername + 1(serverindex) + 10(emails.txt) = + 11
    strcat(file, &sc);
    strcat(file, client);
    char endtxt[11];
    strcpy(endtxt,"emails.txt");
    //open their emails file 
    if ( (fr = fopen( ( strcat(file, endtxt) ) , "r") ) == NULL ) {  //done reading emails from file or no emails
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
        new_cell->status = 'u'; //unread

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
        
        sn++;
    } 
    //send the new_window to the client
    print_window(&new_window);
    int ret = SP_multicast(Mbox, AGREED_MESS, client, 0, sizeof(window), (char*)&new_window);
    printf("sizeof(window) = %ld", sizeof(window));
    printf("ret = %d", ret);
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

//create a func that checks if two points to id are the same

//prints contents of the window
void print_window(window* w)
{
    cell *cell_;
    printf("\tsn#\t<server,seq_num>\tsubject\tmessage\tfrom\n");
    for ( int i = 0; i < MAX_CELLS; i++ ) {
        cell_ = &w->window[i];
        if (cell_ == NULL) {
            printf("\nno cell at window[%d]", i);
        } else {
            printf("\n\t%d\t<%d,%d>\t%s\t%s\t%s", cell_->sn, cell_->mail_id.server, cell_->mail_id.sequence_num, cell_->mail.subject, cell_->mail.message, cell_->mail.sender);
        }
    }

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
