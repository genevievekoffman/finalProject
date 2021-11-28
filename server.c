//server program

#include "structs.h"
#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_VSSETS 10 //?what is this for

static int server_index; //unique server index
static char Private_group[MAX_GROUP_NAME];
static char Server[80];
static mailbox Mbox;
static char Spread_name[80];
static int To_exit = 0;
static char sc;

static void Read_message();
static void Send_message();
static void Bye();
static void request_mailbox(char *client);
static void updates_window_init();
static int repo_insert(linkedList *l, update* u);
void print_repo(linkedList *l);

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

static void Send_message() 
{
    printf("send message");


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
    vs_set_info     vssets[MAX_VSSETS];
    int             num_vs_sets;
    unsigned int    my_vsset_index;
    char            members[MAX_SERVERS][MAX_GROUP_NAME];
    int             ret;
    sc = (server_index + '0'); 

    ret = SP_receive( Mbox, &service_type, sender, 10, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);

    if ( Is_regular_mess( service_type ) )
    {
        printf("\nregular msg\n");
        mess[ret] = 0;
        switch ( mess_type )
        {
            case 0: ; 
                //joins the group server_client
                char *msg = (char*)mess;
                //joins the group server_client
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
                new_update->id = unique_id;
                unique_id->server = server_index;
                unique_id->sequence_num = updates_sent;

                email *new_email = (email*)malloc(sizeof(email)); 
                new_email = (email*)mess;
                new_update->email_ = *new_email;
                
                printf("\nnew_update->email_.to = %s", new_update->email_.to);
                printf("\nnew_update->email_.subject = %s", new_update->email_.subject);
                printf("new_update->email_.message = %s", new_update->email_.message);
                printf("new_update->email_.sender = %s\n", new_update->email_.sender);

                //write the new email to our log/file for that users email file
                //char filename[] = "/tmp/ts_";
                char filename[MAX_USERNAME+11]; //size? maxusername + 1(serverindex) + 10(emails.txt) = + 11
                sprintf(filename, &sc);
                strcat(filename, new_email->to);
                char endtxt[11];
                strcpy(endtxt,"emails.txt");
                if ( (fw = fopen( ( strcat(filename, endtxt) ) , "a") ) == NULL ) { //a appends instead of overwriting the file
                    perror("fopen");
                    exit(0);
                }
                ret = fprintf(fw, "%d %d: to:%s sub:%s msg:%s sender:%s\n", new_update->id->server, new_update->id->sequence_num, new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender);
                if ( ret < 0 )
                    printf("fprintf error");
                fclose(fw);
                
                //TODO: apply the update
                //adds new_update to our linked list in updates_window
                ret = repo_insert(updates_window[server_index-1], new_update);
                if (ret == -1) //error
                    printf("error");
                print_repo(updates_window[server_index-1]);
                
                //update our status_matrix to our seq_num,server_index (unique_id)
                status_matrix[server_index-1][server_index-1] = unique_id;
                
                //multicast the update to all_servers group
                ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 5, sizeof(update), (char*)(new_update));
                //^^not yet tested
                break;
        
            case 2: ;
                printf("\ncase 2");
                break;

            case 4: ;
                //received a mailbox request from client
                char *client = (char*)mess;
                printf("client is %s", client);
                break;

            case 5: ;
                printf("\ncase 5: received an udpate from server on all servers group");
                //ignore update if it's from ourself
                //sender[7] is the index ~ #server1#ugrad8
                if ( server_index == sender[7] ) break;
    

                //First: save this update to the servers log file

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
                //below doesnt matter rn
                if (num_vs_sets < 0)
                {
                    printf("BUG: membership message has more then %d vs sets. Recompile with larger MAX_VSSETS\n", MAX_VSSETS);
                    SP_error( num_vs_sets );
                    exit( 1 );
                }
            }
        
        }else if (Is_transition_mess(   service_type ) ) {
            printf("received TRANSITIONAL membership for group %s\n", sender );
        }else if( Is_caused_leave_mess( service_type ) ){
            printf("received membership message that left group %s\n", sender );
        }else printf("received incorrecty membership message of type 0x%x\n", service_type );
    
    } else printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);

}

/* creates an array of cells filled with a users emails and sends it to the client */
static void request_mailbox(char *client)
{
    cell *new_window[20];
    int sn = 1; //goes up to 20
    FILE *fr; //pointer to file for reading 
    
    sc = (server_index + '0'); 
    
    //char filename[] = "/tmp/ts_";
    char filename[MAX_USERNAME+11]; //size? maxusername + 1(serverindex) + 10(emails.txt) = + 11
    strcat(filename, &sc);
    strcat(filename, client);
    char endtxt[11];
    strcpy(endtxt,"emails.txt");
        //open their emails file 
    if ( (fr = fopen( ( strcat(filename, endtxt) ) , "r") ) == NULL ) { 
        perror("fopen");
        exit(0);
    }
   
    while ( sn <= 20 ) { 
        if ( (fr = fopen( (strcat(filename,"emails.txt") ) , "r") ) == NULL ) {
            //new_window[0] = NULL;
            break;
        }
        //go to eof
        //open serverindex_client_emails file
        //unique id will be first
    }

    //send the new_window to the client
//    int ret = SP_multicast(Mbox, AGREED_MESS,
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
void print_repo(linkedList *l)
{
    if(l->sentinel->nxt == NULL) return;
    node *ptemp = l->sentinel;
    do {
        ptemp = ptemp->nxt;
        printf("\nseq_num=%d, server=%d", ptemp->update->id->sequence_num, ptemp->update->id->server);
    } while(ptemp->nxt != NULL);

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
