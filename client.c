#include "sp.h"
#include "structs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char Spread_name[80];
static int To_exit = 0;
static mailbox Mbox;
static char Private_group[MAX_GROUP_NAME];
static char curr_server[7]; //server currently connected to "server#" 
static char curr_client[MAX_USERNAME]; 
static int logged_in = 0; //1 means there is a use logged in (I didnt know how to check if currclient was null bc it doesnt work ...

static void Bye();
static void Read_message();
static int connected();
static void print_emails();
static void print_menu();
static void User_command();
static void fill_request(request *req, int sn);

static window *client_window;

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
    client_window = malloc(sizeof(client_window));
    
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
        case 'u': ; //logging in
            char username[MAX_USERNAME];
            int ret = sscanf( &command[2], "%s", username);
            if ( ret < 1 ) {
                printf("\ninvalid username\n");
                break;
            }

            //TODO: add further check on if user is already logged in
            sprintf(curr_client, username);
            printf("cc: %s", curr_client);
            //creates its own group username
            ret = SP_join(Mbox, curr_client);
            if ( ret < 0 ) SP_error( ret );
            logged_in = 1;
            //TODO: must request the data from server to fill data structure
            
            //printf("\nusername = %s", username);
            printf("\ncurr_client = %s", curr_client);
            fflush(0);
            break;

        case 'c': ;
            strncpy(curr_server, "server", 7); //resets curr_server to "server "
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

            printf("curr_client = %s", curr_client);
            email *new_msg;
            new_msg = (email*) malloc(sizeof(email));
            //get the recipient/to, subject, & msg
            printf("\n\tto: ");
            fgets(new_msg->to,MAX_USERNAME,stdin);
            //gets rid of '\n' in new_msg->to 
            new_msg->to[strcspn(new_msg->to,"\n")] = 0;
            printf("\n\tsubject: ");
            fgets(new_msg->subject,MAX_USERNAME,stdin);
            new_msg->subject[strcspn(new_msg->subject,"\n")] = 0;
            printf("\n\tmessage: ");
            fgets(new_msg->message,MAX_MESSLEN,stdin);
            new_msg->message[strcspn(new_msg->message,"\n")] = 0;
            sprintf(new_msg->sender, curr_client);
            //printf("\nnew_msg->sender = %s", new_msg->sender);
            
            //sends new email to connected server
            ret = SP_multicast(Mbox, AGREED_MESS, curr_server, 1, sizeof(email), (char*)(new_msg));
            if ( ret < 0 ) SP_error( ret );
            break;

        case 'd': ;
            //ASSUMPTION: client has already called 'l' so they know the serial numbers of mails
            if (logged_in == 0) {
                printf("\n...no user logged in ... \n");
                break;
            }

            char mailsn[2];
            ret = sscanf( &command[2], "%s", mailsn );
            if (ret < 1 || ret > 2) {
                printf("\ninvalid serial number\n");
                break;
            }
            
            request req_;
            fill_request(&req_,atoi(mailsn));
            
            ret = SP_multicast(Mbox, AGREED_MESS, curr_server, 3, sizeof(request), (char*)(&req_));
            if ( ret < 0 ) SP_error( ret );
            
            break;
        
        case 'r': ;
            //ASSUMPTION: client has already called 'l' so they know the serial numbers of mails
            if (logged_in == 0) {
                printf("\n...no user logged in ... \n");
                break;
            }
            
            char sn_[2]; //at most 2 bytes
            ret = sscanf( &command[2], "%s", sn_);
            if ( ret < 0 ) { //todo: add check that its between 1 & 20 
                printf("Missing a serial number\n");
                break;
            }
           
            //prints the contents of that message
            cell *req_cell;
            req_cell = &client_window->window[atoi(sn_)-1];
            printf("\n\tmessage: %s\n", req_cell->mail.message); 

            request req;
            fill_request(&req,atoi(sn_));
            printf("\nreq->mail_id.server=%d,req->mail_id.sequence_num=%d", req.mail_id.server, req.mail_id.sequence_num); 
            ret = SP_multicast(Mbox, AGREED_MESS, curr_server, 2, sizeof(request), (char*)(&req));
            if ( ret < 0 ) SP_error( ret );
            
            //send the update to the server
            break;

        case 'l': ;
            //must be logged in
            if ( logged_in == 0 || connected() == -1 ) {
                printf("\n\terror: either not logged in or no connection with server\n");
                break;
            }
            printf("curr_client = %s", curr_client);
            fflush(0);
            ret = SP_multicast( Mbox, AGREED_MESS, curr_server, 4, sizeof(curr_client), curr_client);

            if ( ret < 0 ) {
                SP_error( ret );
                Bye();
            }

            break;

        default:
            printf("\nUnknown command\n");
            break;
    }
}

static void Read_message()
{
    static char    mess[sizeof(window)]; //reading windows at most
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
        mess[ret] = 0;
        switch ( mess_type )
        {
            case 0: ;
                client_window = (window*)mess;
                print_emails();
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

//issue: why does it print the last one after user has claled another cmnd
/* prints out contents of client_window */
static void print_emails()
{
    printf("Mailbox of @%s\n", curr_client); 
    printf("Connected to %s\n", curr_server);

    cell *cell_;
    for( int i = 0; i < MAX_CELLS; i++ ) {
        cell_ = &client_window->window[i];
        if(cell_->sn == 0) {
            return;
        } else {
            //TODO: ONLY NEED TO PRINT THE SENDER & SUBJECT
            printf("\n\t%d\t<%d,%d>\t%s\t%s\t%s\n\t%c\n", cell_->sn, cell_->mail_id.server, cell_->mail_id.sequence_num, cell_->mail.subject, cell_->mail.message, cell_->mail.sender, cell_->status);
        }
    }

}

//fills the information within the request type
static void fill_request(request *req, int sn)
{
    cell *temp_cell = &client_window->window[sn-1];
    printf("\ngrabbed cell with id = <%d,%d>\n", temp_cell->mail_id.server, temp_cell->mail_id.sequence_num);
    strcpy(req->user, curr_client);
    req->mail_id.server = temp_cell->mail_id.server;
    req->mail_id.sequence_num = temp_cell->mail_id.sequence_num;
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
    free(client_window);
    printf("\nBye.\n");
    SP_disconnect( Mbox );
    exit(0);
}
