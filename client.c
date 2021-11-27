//client program

#include "sp.h"
#include "structs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void print_emails();
static void print_menu();
static void User_command();
static char Spread_name[80];
static int To_exit = 0;
static mailbox Mbox;
static char Private_group[MAX_GROUP_NAME];
static char curr_server[10]; //server currently connected to "server#" 
static char curr_client[MAX_USERNAME]; 
static void Bye();
static void Read_message();
static int connected();

static int logged_in = 0; //1 means there is a use logged in (I didnt know how to check if currclient was null bc it doesnt work ...

//static email *new_msg;

static cell *client_window[20];

int main( int argc, char *argv[] ) 
{
    sp_time test_timeout;
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    sprintf( Spread_name, "10050"); //always connects to my port
    
    int ret;
    /* connect to SPREAD */
    ret = SP_connect_timeout( Spread_name, "client", 0, 1, &Mbox, Private_group, test_timeout ); 
    if( ret != ACCEPT_SESSION ) {
        SP_error( ret );
        Bye();
    }
    printf("Client: connected to %s with private group %s\n", Spread_name, Private_group );

    print_menu();
    
    E_init();
    E_attach_fd( 0, READ_FD, User_command, 0, NULL, HIGH_PRIORITY); //users control has highest priority
    E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, LOW_PRIORITY );
    E_handle_events();

}

static void User_command()
{
    char command[130];

    for(int i = 0; i < sizeof(command); i++) command[i] = 0;
    fgets(command,130,stdin);

    switch (command[0])
    {
        case 'u': ;
            /*NEED TO CONNECT TO A DEFAULT SERVER WHEN LOGGING IN...*/

            //logging in
            char username[MAX_USERNAME];
            int ret = sscanf( &command[2], "%s", username );
            if ( ret < 1 ) {
                printf("\ninvalid username\n");
                break;
            }

            //TODO: add further check on if user is already logged in
            sprintf(curr_client, username);

            //creates its own group username
            ret = SP_join(Mbox, username);
            if ( ret < 0 ) SP_error( ret );
            logged_in = 1;
            //TODO: must request the data from server to fill data structure

            break;

        case 'c': ;
            strncpy(curr_server, "server", MAX_GROUP_NAME); //resets the server name
            //connecting to a mail server
            if ( logged_in == 0 ) {
                printf("\n...no user logged in ... \n");
                break;
            }
            char server[MAX_USERNAME];
            ret = sscanf( &command[2], "%s", server);
            
            int s = atoi(server);
            //make sure it's just one int & 1-5
            if ( ret != 1 || s > 5 || s < 1 ) { 
                printf("\ninvalid server number\n");
                break;
            }

            //joins the server_client group: 1genkoffman
            strncat( server, curr_client, sizeof(curr_client) );
            ret = SP_join(Mbox, server);
            if ( ret < 0 ) SP_error( ret );
            strncat(curr_server, &server[0], 1); //just the first char (index)
            //requests to connect with that server
            ret = SP_multicast(Mbox, AGREED_MESS, curr_server, 0, sizeof(curr_client), curr_client);
            if ( ret < 0 ) SP_error( ret );
            
            //TODO: waits until it gets a membership notification that says the server joined the group
                //then we can update the curr_server (connection etablished)
            //can we just set the curr_server or do we need to wait for connection join notification on that group ...
            break;

        case 'm': ;
            //sending an email
            if ( logged_in == 0 || connected() == -1 ) { 
                printf("\n...no user logged in or no connection with server... \n");
                break;
            }

            email *new_msg;
            new_msg = (email*) malloc(sizeof(email));
            //get the recipient/to, subject, & msg
            printf("\n\tto: ");
            fgets(new_msg->to,MAX_USERNAME,stdin);
            //gets rid of '\n' in new_msg
            new_msg->to[strcspn(new_msg->to,"\n")] = 0;
            printf("\n\tsubject: ");
            fgets(new_msg->subject,BYTES,stdin);
            printf("\n\tmessage: ");
            fgets(new_msg->message,MAX_MESSLEN,stdin);
            sprintf(new_msg->sender, curr_client);
            printf("\nnew_msg->sender = %s", new_msg->sender);
            printf("new_msg->to= %s", new_msg->to);
            printf("new_msg->subject= %s", new_msg->subject);
            printf("new_msg->msg = %s", new_msg->message);
            
            //sends new email to connected server
            ret = SP_multicast(Mbox, AGREED_MESS, curr_server, 1, sizeof(email), (char*)(new_msg));
            if ( ret < 0 ) SP_error( ret );
            
            //just a test to see if print works
            /*
            cell test;
            test.sn = 1;
            test.status = 'u';
            test.contents = new_mail;
            client_window[0] = &test;
            */

            break;

        case 'd': ;
            
            printf("cmnd = d\n");
            if (logged_in == 0) {
                printf("\n...no user logged in ... \n");
                break;
            }

            char mailsn[5];
        
            ret = sscanf( &command[2], "%s", mailsn );
            if (ret < 1 || ret > 2) {
                printf("\ninvalid serial number\n");
                break;
            }
            //can be an int 1-20
            
            //grab the emails id with sn: mailsn
            //sn is just i-1
            int sn = atoi(mailsn);
            printf("sn converted is %d\n", sn);
            if ( client_window[sn-1] != NULL ) {
                //set its status to delete
                //send an update to server

            }
                
            break;
        case 'r': ;
            printf("cmnd = r\n");
            break;

        case 'l': ;
            printf("cmnd = l\n");
            //must be logged in
            if ( logged_in == 0 || connected() == -1 ) {
                printf("\n\terror: either not logged in or no connection with server\n");
                break;
            }
            ret = SP_multicast( Mbox, AGREED_MESS, curr_server, 4, sizeof(curr_client), curr_client);

            if ( ret < 0 ) {
                SP_error( ret );
                Bye();
            }

            //now it needs to retreive an up to date mailbox from server
            Read_message();
            //testing
            print_emails();
            break;

        default:
            printf("\nUnknown command\n");
            break;
    }
}

static void Read_message()
{
    static char    mess[MAX_MESSLEN];
    char		   sender[MAX_GROUP_NAME];
    char           target_groups[MAX_SERVERS][MAX_GROUP_NAME];
    int            service_type;
    int16          mess_type;
    int            endian_mismatch;
    int            num_groups;
    membership_info memb_info;
    int ret;

    ret = SP_receive(Mbox, &service_type, sender, 10, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess); 

    if ( Is_regular_mess( service_type ) )
    {
        printf("\nregular msg\n");
        mess[ret] = 0;
        switch ( mess_type )
        {
            case 0: ;
                //only message they get is an array of size 20 filled with cells from curr_server
                printf("case 0: we received a cell update");

                //TODO: RECEIVE ARRAY OF CELLS AND UPDATE OUR LOCAL ONE

                break;
            default: ;
                printf("unkown ");
        }
    }else if (Is_membership_mess( service_type ) )
    {
        printf("\nmembership msg\n");
        ret = SP_get_memb_info( mess, service_type, &memb_info );
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
                printf("Due to the LEAVE of %s\n", memb_info.changed_member );
            } else if( Is_caused_disconnect_mess( service_type ) ){
                printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
                    
                    //need to request for a new server to connect to!
                    printf("\nPlease connect to a new server");
                    curr_server[0] = ' ';
            } else {

            }
        }else if (Is_transition_mess(   service_type ) ) { 
            printf("received TRANSITIONAL membership for group %s\n", sender );
        }else if( Is_caused_leave_mess( service_type ) ){
            printf("received membership message that left group %s\n", sender );
        }else printf("received incorrecty membership message of type 0x%x\n", service_type );
    } else printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);


}

/* prints out contents of client_window */
static void print_emails()
{
    for( int i = 0; i < 20; i++ ) {
        cell *email = client_window[i];
        if(email == NULL) {
            if (i == 0) printf("\tno mail");
            return;
        }
        printf("\tsn=%d:", email->sn);
        printf(" sender=%s", email->contents->sender);
        printf(", subject=%s", email->contents->subject);
    }

}

static void print_menu()
{
    printf("\n");
    printf("User Menu:\n");
    printf("----------\n");
    printf("\n\tu <usernmae> -- login with username\n");
    printf("\tc <server> -- connect with mail server\n");
    printf("\tl -- list received mail\n");
    printf("\tm -- write an email\n");
    printf("\td <email sn> -- delete an email\n");
    printf("\tr <email sn> -- read an email\n");
    printf("\tv -- prints memberships in current mail server\n");
    fflush(stdout);
}

//returns 0 if connected to a server else -1
int connected()
{
    return (curr_server[0] == 's') ? 0 : -1;
}

static void Bye()
{
    To_exit = 1;
    //free(new_msg);
    printf("\nBye.\n");
    SP_disconnect( Mbox );
    exit(0);
}
