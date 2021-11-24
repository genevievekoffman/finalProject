//server program

#include "structs.h"
#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SERVERS 5
#define MAX_VSSETS 10 //?what is this for
#define MAX_MESSLEN     5000

static int server_index; //unique server index
static char Private_group[MAX_GROUP_NAME];
static char Server[80];
static mailbox Mbox;
static char Spread_name[80];
static int To_exit = 0;
static char *buf;
static int msg_size = sizeof(update);

char sender[MAX_GROUP_NAME]; //either a server or user

static void Read_message();
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

    updates_sent = 0; //initialize to 0
    new_update = malloc(sizeof(update));
    
    //handle arguments
    if ( argc != 2 ) {
        printf("Usage: server <1-5>\n");
        exit(0);
    }

    server_index = atoi(argv[1]); 
    sprintf( Server, "server" );
    sprintf( Spread_name, "10050"); //always connects to my port
    strncat(Server, argv[1], 1);
    sprintf(Private_group,argv[1]); //init
    
    int ret;
    /* connect to SPREAD */ 
    ret = SP_connect_timeout( Spread_name, Server, 0, 1, &Mbox, Private_group, test_timeout ); 
                                    //private_group is returned group name(used for unicast msgs only)
    if( ret != ACCEPT_SESSION ) {
        SP_error( ret );
        Bye();
    }
    printf("\n\t>Server: connected to %s with private group %s\n", Spread_name, Private_group );
    
    E_init();
    E_attach_fd(Mbox,READ_FD, Read_message,0,NULL,HIGH_PRIORITY); //select
    //joins its own public group ex: S1, S2 ... (clients will request connection here)
    ret = SP_join( Mbox, Server );
    if ( ret < 0 ) SP_error( ret );
    printf("\njoined its own public group: %s\n", Server);
    //joins the network group with all servers in it
    ret = SP_join( Mbox, "all_servers");
    if ( ret < 0 ) SP_error( ret );
    
    E_handle_events();

}

static void Read_message()
{
    static  char    mess[MAX_MESSLEN];
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
    service_type = 0;

    //can receive from all_servers or any client connected to it
    ret = SP_receive( Mbox, &service_type, sender, 10, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess);
    
    if ( Is_regular_mess( service_type ) )
    {
        printf("\nregular msg\n");
        mess[ret] = 0;
        switch ( mess_type )
        {
            case 0: ;
                printf("\n0");
                break;
            case 1: ;
                //1 = new email 
                //is it from a client or another server?
                //check the sender -> 
                
                //if it's from a client: (not server 1 through 5)
                    //create a new update/fill its data
                    new_update->type = 1; //new email
                    new_update->server = server_index;
                    updates_sent++;
                    new_update->sequence_num = updates_sent;
                    email *new_email = (email*)buf;

                    new_update->email_ = *new_email;
                
                    char filename[] = "/tmp/ts_";
                    strcat(filename, server_index);
                    strcat(filename, new_email->to);

                    //write the new email to our log/file for that users email file
                    if ( (fw = fopen( (strcat(filename,"emails.txt") ) , "w") ) == NULL ) {
                        perror("fopen");
                        exit(0);
                    }
                    //write to the top of the file
                    fprintf(fw,"%d %d", new_update->server, new_update->sequence_num); //server, sequence_num : to, subject, msg, sender
                //if it's from another server:
                printf("\n1");
                break;
        }
        printf("reg msg");
        
        //switch 

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
                printf("Due to NETWORK change with %u VS sets\n", memb_info.num_vs_sets);
                num_vs_sets = SP_get_vs_sets_info( buf, &vssets[0], MAX_VSSETS, &my_vsset_index );
                if (num_vs_sets < 0)
                {
                    printf("BUG: membership message has more then %d vs sets. Recompile with larger MAX_VSSETS\n", MAX_VSSETS);
                    SP_error( num_vs_sets );
                    exit( 1 );
                }
                for (int i = 0; i < num_vs_sets; i++ )
                {
                    printf("%s VS set %d has %u members:\n",
                        (i  == my_vsset_index) ?
                        ("LOCAL") : ("OTHER"), i, vssets[i].num_members );
                    ret = SP_get_vs_set_members(buf, &vssets[i], members, MAX_SERVERS);
                    if (ret < 0) {
                        printf("VS Set has more then %d members. Recompile with larger MAX_MEMBERS\n", MAX_SERVERS);
                        SP_error( ret );
                        exit( 1 );
                    }
                    for ( int j = 0; j < vssets[i].num_members; j++ )
                        printf("\t%s\n", members[j] );
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
    char filename[] = "/tmp/ts_";
    strcat(filename, &server_index);
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
    SP_disconnect( Mbox );
    exit(0);
}
