//client program

#include "sp.h"
#include "structs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_USERNAME 20

static void print_emails();
static void print_menu();
static void User_command();
static char Spread_name[80];
static int To_exit = 0;
static mailbox Mbox;
static char Private_group[MAX_GROUP_NAME];
static char curr_server[] = "server"; //server index currently connected to 
static char curr_client[MAX_USERNAME]; //username of the current client logged in
static void Bye();
static void Read_message();

static int logged_in = 0; //1 means there is a use logged in (I didnt know how to check if currclient was null bc it doesnt work ...
static email *new_mail;

//internal DS: array holding 20 cells (pointer to cells)
static cell *client_window[20];

int main( int argc, char *argv[] ) 
{
    sp_time test_timeout;
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    
    new_mail = malloc(sizeof(email));

    sprintf( Spread_name, "10050"); //always connects to my port
    
    int ret;
    /* connect to SPREAD */
    ret = SP_connect_timeout( Spread_name, "client", 0, 1, &Mbox, Private_group, test_timeout ); 
    if( ret != ACCEPT_SESSION ) {
        SP_error( ret );
        Bye();
    }
    printf("Client: connected to %s with private group %s\n", Spread_name, Private_group );

    E_init();
    E_attach_fd( 0, READ_FD, User_command, 0, NULL, HIGH_PRIORITY); //users control has highest priority
    E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, LOW_PRIORITY );

    //need to connect to a server 
    print_menu();
    for(;;)
        User_command();
}

static void User_command()
{
    char command[130];

    for(int i = 0; i < sizeof(command); i++) command[i] = 0;
    fgets(command,130,stdin);

    switch (command[0])
    {
        case 'u': ;
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
            //connecting to a mail server
            if ( logged_in == 0) {
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
            printf("\njoining server: %s", curr_server);
            ret = SP_join(Mbox, curr_server); 
            if ( ret < 0 ) SP_error( ret );
            
            //TODO: waits until it gets a membership notification that says the server joined the group
                //then we can update the curr_server (connection etablished)
            //sprintf(curr_server, input[0]); 
             
            break;

        case 'm': ;
            //sending an email
            if ( logged_in == 0) {
                printf("\n...no user logged in ... \n");
                break;
            }
            
            //get the recipient/to, subject, & msg
            sprintf(new_mail->sender, curr_client);
            printf("\n\tto: ");
            fgets(new_mail->to,MAX_USERNAME,stdin);
            printf("\n\tsubject: ");
            fgets(new_mail->subject,MAX_CHAR,stdin);
            printf("\n\tmessage: ");
            fgets(new_mail->message,250,stdin);
                
            /*
            printf("\nnew email:");
            printf("\n\tto= %s", new_mail->to);
            printf("\n\tsubject = %s", new_mail->subject);
            printf("\n\tmsg = %s", new_mail->message);
            */
            
            //TODO: send this email to the servers group
            //server will create a new update and put the email in it 
            ret = SP_multicast(Mbox, AGREED_MESS, curr_server, 1, sizeof(email), (char*)new_mail); //tag 1 = new email from client

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
        case 'r':

        case 'l': ;
            //must be logged in
            if ( logged_in == 0) {
                printf("\n...no user logged in ... \n");
                break;
            }
            ret = SP_multicast( Mbox, AGREED_MESS, curr_server, 4, sizeof(curr_client), curr_client); //tag 4 = requesting list of emails to refill our client_window
            if ( ret < 0 ) {
                SP_error( ret );
                Bye();
            }
            
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

static void Bye()
{
    To_exit = 1;
    free(new_mail);
    printf("\nBye.\n");
    SP_disconnect( Mbox );
    exit(0);
}
