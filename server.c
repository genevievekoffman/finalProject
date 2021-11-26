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

static update *new_update;
static int updates_sent; //sequence num
FILE *fw;

int main(int argc, char **argv)
{
    sp_time test_timeout;
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    updates_sent = 0; 
    new_update = malloc(sizeof(update));
    
    //handle arguments
    if ( argc != 2 ) {
        printf("Usage: server <1-5>\n");
        exit(0);
    }

    server_index = atoi(argv[1]); 
    sprintf( Server, "server" );
    sprintf( Spread_name, "10050"); //always connects to my port
    strncat( Server, argv[1], 1 );
    sprintf( Private_group, argv[1] ); //init
    
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
    printf("Read_message()\n");
    static  char    mess[sizeof(update)]; //the biggest it can be ...
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
        printf("mess_typ = %d", mess_type);
        switch ( mess_type )
        {
            case 0: ; 
                printf("case 0\n");
                //join the group server_client
                char *msg = (char*)mess;
                //joins the group server_client
                char sc_group[MAX_MESSLEN];
                sprintf(sc_group, &sc);
                strcat(sc_group, msg);
                int ret = SP_join(Mbox, sc_group);
                if ( ret < 0 ) SP_error( ret ); 
                break;
                
            case 1: ;
                //1 = new email from a client  
                //create a new update/fill its data
                /*
                new_update->type = 1; //new email
                new_update->server = server_index;
                updates_sent++;
                new_update->sequence_num = updates_sent;
                */

                email *new_email = (email*)malloc(sizeof(email)); 
                new_email = (email*)mess;
                
                printf("\nnew_email->to = %s", new_email->to);
                printf("new_email->subject = %s", new_email->subject);
                printf("new_email->message = %s", new_email->message);
                printf("new_email->sender = %s\n", new_email->sender);
               
                break;

                /*
                new_update->email_ = *new_email;
                
                char filename[] = "/tmp/ts_";
                strcat(filename, &sc);
                strcat(filename, new_email->to);

                //write the new email to our log/file for that users email file
                if ( (fw = fopen( (strcat(filename,"emails.txt") ) , "w") ) == NULL ) {
                    perror("fopen");
                    exit(0);
                }
                //write to the top of the file
                fprintf(fw,"%d %d", new_update->server, new_update->sequence_num); //server, sequence_num : to, subject, msg, sender
                */
                //break;
        
            case 2: ;
                printf("\ncase 2");
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

/*when a client requests to see their mail, client is the name of the user logged in*/
static void request_mailbox(char *client)
{
    cell new_window[20];
    int sn = 1; //goes up to 20
    FILE *fr; //pointer to file for reading 
    
    sc = (server_index + '0'); 
    
    char filename[] = "/tmp/ts_";
    strcat(filename, &sc);
    strcat(filename, client);
    
    while ( sn <= 20 ) { 
        //open their emails file 
        if ( (fr = fopen( (strcat(filename,"emails.txt") ) , "r") ) == NULL ) {
            //nothing to read
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

static void Bye()
{
    To_exit = 1;
    free(new_update);
    printf("\nBye.\n");
    //E_detach_fd( Mbox, READ_FD ); 
    SP_disconnect( Mbox );
    exit(0);
}
