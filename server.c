//server program
#include "structs.h"
#include "sp.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_VSSETS 10 
#define MAX_LEN 256 //max line in file

static void print_email(email *m);
static void print_update(update *u);
static void print_id(id *id_);
static void print_matrix();
static void print_repo(int i);
static void print_window(window* w);

static int server_index; //unique server index
static char Private_group[MAX_GROUP_NAME];
static char Server[80];
static mailbox Mbox;
static char Spread_name[80];
static int To_exit = 0;
static char sc;
static int state; //0 = normal state 1 = reconciliation state
static int ret;
FILE *fw;
FILE *fr; //pointer to file for reading 

static void Read_message();
static void Bye();
static void request_mailbox(char *client);
static void updates_window_init();
static int repo_insert(int i, update* u);
static int check_status(id *id_, char *file);
static void write_to_log(char *sec_server);
static void apply_update();
static void dummy_join(membership_info *mem_info);
static void dummy_leave(membership_info *mem_info);
static void recon();
static void get_filename(char *fn, char *client, int r);
static update *new_update;
static matrixStatus *new_matrix;
static int updates_sent; //sequence num
static linkedList updates_window[MAX_SERVERS]; 
static int status_matrix[MAX_SERVERS][MAX_SERVERS]; 
static char log_filename[9];
static matrixStatus recon_matrices[MAX_SERVERS]; //used during reconcilation to hold other servers incoming status matrices
static void resend_updates(int server, int min_sn, int max_sn);
//used to keep track of how many incoming matrices we have
int recon_num; //unique reconciliation num
int matrices_needed;
char recon_target_groups[MAX_SERVERS][MAX_GROUP_NAME]; //used during recon (saves membership info to use after receiving matrices)
static void reboot_logs();
static int network_memb[MAX_SERVERS]; //each spot has a 0 or 1 (1 means they are in the current network, 0 is not)
static int in_network(int server);

int main(int argc, char **argv)
{
    sp_time test_timeout;
    test_timeout.sec = 5;
    test_timeout.usec = 0;
    //updates_sent = 0; 

    //fills with 0's
    for ( int r = 0; r < MAX_SERVERS; r++ ) {
        for ( int c = 0; c < MAX_SERVERS; c++ ) {
            status_matrix[r][c] = 0;
        }
    }

    if ( argc != 2 ) {
        printf("Usage: server <1-5>\n");
        exit(0);
    }

    server_index = atoi(argv[1]); 
    sprintf( Server, "server" );
    sprintf( Spread_name, "10050"); //always connects to my port
    strncat( Server, argv[1], 1 );
    sprintf( Private_group, argv[1] );
    sc = server_index + '0'; 
    reboot_logs();
    updates_sent = status_matrix[server_index-1][server_index-1];
    printf("after rebooting, updates sent = %d", updates_sent);
    /* connect to SPREAD */ 
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
    char recipients_file[MAX_USERNAME+11];
    char *client;

    sc = (server_index + '0'); 
    new_update = (update*)malloc(sizeof(update));
    new_matrix = (matrixStatus*)malloc(sizeof(matrixStatus));
    ret = SP_receive( Mbox, &service_type, sender, 10, &num_groups, target_groups, &mess_type, &endian_mismatch, sizeof(mess), mess );

    if ( Is_regular_mess( service_type ) )
    {
        mess[ret] = 0;
        switch ( mess_type )
        {
            case 0: ; //client connection request 
                //joins the group server_client
                char *msg = (char*)mess;
                char sc_group[MAX_MESSLEN];
                sprintf(sc_group, &sc);
                strcat(sc_group, msg);
                ret = SP_join(Mbox, sc_group);
                if ( ret < 0 ) SP_error( ret ); 
                break;
                
            case 1: ; //new email from a client  
                email *new_email = (email*)mess;
                //printf("\nReceived a new email from client\n\t");
                print_email(new_email);

                //clear the new_update
                memset(new_update, '\0', sizeof(update));
                strcpy(new_update->email_.to, new_email->to);
                strcpy(new_update->email_.subject, new_email->subject);
                strcpy(new_update->email_.message, new_email->message);
                strcpy(new_update->email_.sender, new_email->sender);
                //print_email(&new_update->email_);
                updates_sent++;
                new_update->mail_id.server = server_index;
                new_update->mail_id.sequence_num = updates_sent;
                new_update->update_id.sequence_num = updates_sent;
                new_update->update_id.server = server_index;
                new_update->type = 1; 
                //write the update to OUR log file ##LOG.txt
                write_to_log(&sc);
                //writes the email in the recipients emails.txt file
                char filename[MAX_USERNAME+11] = "";
                //printf("\nbefore getfilename(): new_update->email_.to = %s", new_update->email_.to);
                get_filename(filename, new_update->email_.to, 0); //0=emails.txt
                if ( ( fw = fopen(filename, "a") ) == NULL ) {
                    perror("fopen");
                    exit(0);
                }
                printf("\nWriting new email to FILE:%s\n\t %d %d\n\t", filename, new_update->mail_id.server, new_update->mail_id.sequence_num); 
                print_email(&new_update->email_);
                ret = fprintf(fw, "%d %d|%s|%s|%s|%s\n", new_update->mail_id.server, new_update->mail_id.sequence_num, new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender); //server seq_num|to|subject|msg|sender
                if ( ret < 0 ) printf("fprintf error");
                fclose(fw);
                
                //apply the update
                apply_update();
                //multicast the update to all_servers group
                ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 5, sizeof(update), (char*)(new_update));
                break;
        
            case 2: ; //received a read request from client
                request *temp_req = (request*)mess;
                //memset(new_update, 0, sizeof(update)); //fill it with 0's
                new_update->mail_id.server = temp_req->mail_id.server;
                printf("\nA) mail_id:");
                print_id(&temp_req->mail_id);
                
                new_update->mail_id.sequence_num = temp_req->mail_id.sequence_num;

                updates_sent++;
                new_update->type = 2; //2 read an email request
                new_update->update_id.server = server_index;
                new_update->update_id.sequence_num = updates_sent; 
                
                //printf("READ WRITING1: %d %d\n", new_update->mail_id.server, new_update->mail_id.sequence_num);
                strcpy(new_update->email_.to, temp_req->user);
                
                //write update to OUR log file ##LOG.txt
                write_to_log(&sc);

                char fn[MAX_USERNAME+11] = "";
                get_filename(fn, new_update->email_.to, 1); //1=reads.txt
                if ( ( fw = fopen(fn, "a") ) == NULL ) {
                    perror("fopen");
                    exit(0);
                }
                //write the emails id in the recipients read.txt file
                ret = fprintf(fw, "%d %d\n", new_update->mail_id.server, new_update->mail_id.sequence_num);
                if ( ret < 0 )
                    printf("fprintf error");
                fclose(fw);
                apply_update();
                //multicast the update to all_servers group
                ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 5, sizeof(update), (char*)(new_update));
                break;

            case 3: ; //received a delete request from client
                request *tempreq = (request*)mess;
                new_update->mail_id.server = tempreq->mail_id.server;
                new_update->mail_id.sequence_num = tempreq->mail_id.sequence_num;

                updates_sent++;
                new_update->type = 3; //delete an email request
                new_update->update_id.server = server_index;
                new_update->update_id.sequence_num = updates_sent; //it would be server, seq num
                strcpy(new_update->email_.to, tempreq->user);
                
                //write update to OUR log file ##LOG.txt
                write_to_log(&sc);
                char fn_[MAX_USERNAME+11] = "";
                get_filename(fn_, new_update->email_.to, 2); //2 = deletes.txt
                if ( ( fw = fopen(fn_, "a") ) == NULL ) {
                    perror("fopen");
                    exit(0);
                }
                //write the emails id in here
                ret = fprintf(fw, "%d %d\n", new_update->mail_id.server, new_update->mail_id.sequence_num);
                fclose(fw);
                
                apply_update(); 

                //multicast the update to all_servers group
                ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 5, sizeof(update), (char*)(new_update));
                break;
            
            case 4: ; //received a mailbox request from client
                client = (char*)mess;
                request_mailbox(client);
                break;

            case 5: ;
                //printf("\nReceived an update from a server on all_servers group");
                //ignore update if it's from ourself
                if ( server_index == atoi(&sender[7]) ) break;
                new_update= (update*)mess;

                //if we are in reconcilation state it is possible that we don't need this missing update 
                int temp_ser = new_update->update_id.server;
                int temp_sn = new_update->update_id.sequence_num;
                //printf("the highest id sequence num i have for server %d = %d",temp_ser, status_matrix[server_index-1][temp_ser-1]);
                if ( status_matrix[server_index-1][temp_ser-1] >= temp_sn ) 
                    break;
                //print_update(new_update);
                
                int origin_server = new_update->update_id.server; 
                //write the update to our log file for the server that created the update
                char charVal = origin_server + '0';
                write_to_log(&charVal);
                
                //add update to the linked list in our matrix (at the server who created it index)
                repo_insert(origin_server - 1, new_update);
                status_matrix[server_index-1][origin_server- 1] = new_update->update_id.sequence_num;
                print_matrix();
                print_repo(origin_server - 1);

                //apply the update based on type
                switch(new_update->type) {
                    case 1: ; //new email -> save the emails info to its log file
                        memset(recipients_file, '\n', MAX_USERNAME+11); 
                        get_filename(recipients_file, new_update->email_.to, 0); //0=emails.txt
                        if ( ( fw = fopen(recipients_file, "a") ) == NULL ) {
                            perror("fopen");
                            exit(0);
                        }
                        ret = fprintf(fw, "%d %d|%s|%s|%s|%s\n", new_update->mail_id.server, new_update->mail_id.sequence_num, new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender); //server seq_num|to|subject|msg|sender
                        if ( ret < 0 ) printf("fprintf error");
                        fclose(fw);
                        break;
                    case 2: ; //new read email update -> write the emails id in the recipients read.txt file
                        memset(recipients_file, '\n', MAX_USERNAME+11);
                        get_filename(recipients_file, new_update->email_.to, 1); //1=reads.txt
                        if ( ( fw = fopen(recipients_file, "a") ) == NULL ) {
                            perror("fopen");
                            exit(0);
                        }
                        ret = fprintf(fw, "%d %d\n", new_update->mail_id.server, new_update->mail_id.sequence_num);
                        fclose(fw);
                        break;
                    case 3: ;//new delete email update -> write the emails id in the recipients delete.txt file
                        memset(recipients_file, '\n', MAX_USERNAME+11);
                        get_filename(recipients_file, new_update->email_.to, 2); //2=deletes.txt
                        //printf("\n\trecipients_file = %s\n", recipients_file);
                        if ( ( fw = fopen(recipients_file, "a") ) == NULL ) {
                            perror("fopen");
                            exit(0);
                        }
                        ret = fprintf(fw, "%d %d\n", new_update->mail_id.server, new_update->mail_id.sequence_num);
                        fclose(fw);
                        break;
                }
                break;

            case 6: ; //matrices sent during recon mode
                if (state == 0) printf("\nERROR IN RECON STATE");
                
                for( int i = 0; i < MAX_SERVERS; i++ )
                    printf("\t%s\n", &recon_target_groups[i][0] );
                
                new_matrix = (matrixStatus*)mess;
                
                if ( new_matrix->membership != recon_num ) { //incorrect round
                    printf("\nwrong recon round");
                    break;
                }

                //add the incoming matrix to our recon_matrices at the index of the sender
                memcpy( &recon_matrices[atoi(&sender[7]) - 1], new_matrix, sizeof(matrixStatus) ); 
                matrices_needed--; 

                //once we've received the needed # of matrices, we can start compare their IDS
                if ( matrices_needed == 0 ) { //have all of them
                    int min[MAX_SERVERS] = {0};
                    int max[MAX_SERVERS] = {0};
                    int sender[MAX_SERVERS] = {0}; //the server to resend missing updates
                    //compare each incoming matrices column
                    for ( int j = 0; j < MAX_SERVERS; j++ ) {
                        for ( int i = 0; i < MAX_SERVERS; i++ ) {
                            //for each member in the group
                            int server_ = atoi(&(&recon_target_groups[i][0])[7]);//server
                            if ( server_ == 0 ) //no server
                                continue;
                            
                            matrixStatus *curr_mat = &recon_matrices[server_-1];
                            //go to their row(server index), whatever column
                            
                            int id_val = curr_mat->id_matrix[server_-1][j];
                            
                            //check if it is a new min or max
                            if ( id_val > max[j] && in_network(server_) == 1 ) { //they must also be in the partition!!
                                //printf("...%d is > max[%d]=%d, updating it\n",id_val,j,max[j]);
                                max[j] = id_val;
                                sender[j] = server_; //server who will resend missing updates
                            }
                            
                            if ( id_val < min[j] ) 
                                min[j] = id_val;
                        }
                        //calculated a min & max for j
                        if ( sender[j] == server_index ) //I need to send the missing updates from min-max
                            resend_updates(j+1, min[j]+1, max[j]);
                    }
                    state = 0; //state goes back to normal
                    print_matrix();
                }
                fflush(0);
                break;
            
            case 7: ; //received a 'v' from client
                client = (char*)mess;
                printf("\nsending to %s: ", client);
                char strbuff[MAX_SERVERS] = "";
                for( int i = 0; i < MAX_SERVERS; i++ ) { 
                    printf("%d", network_memb[i]);
                    char charValue = network_memb[i]+'0';
                    strcat(strbuff, &charValue);
                }
                //sprintf(strbuff, "%d%d%d%d%d", network_memb[0], network_memb[1], network_memb[2], network_memb[3], network_memb[4]);
                //printf("strbuf = %s", strbuff);
                ret = SP_multicast( Mbox, AGREED_MESS, client, 1, sizeof(strbuff), strbuff);
                if (ret < 0 ) SP_error( ret );
                break;
            
            default:
                printf("default unknown msg");
                break;
        }
    } else if (Is_membership_mess( service_type ) )
    {
        printf("\nmembership msg\n");
        int trigger = 0; //0 is server, 1 is client
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
            
            if ( atoi(&sender[0]) == server_index ) //server_client group -> this is a Client trigger change (no recon ever needed) 
                trigger = 1; 
            for ( int i = 0; i < num_groups; i++ )
                printf("\t%s\n", &target_groups[i][0] );
            printf("grp id is %d %d %d\n",memb_info.gid.id[0], memb_info.gid.id[1], memb_info.gid.id[2] );  
            //^^last int seems to be a unique 'round' for every membership change in a group
            
            matrices_needed = num_groups; //used in case we enter recon to track num of matrices we have
            memcpy(recon_target_groups, target_groups, sizeof(char)*MAX_SERVERS*MAX_GROUP_NAME);
            
            //if a server triggered the change -> save the memb_info to be used later
            if ( trigger == 0 ) {
                memset( &network_memb, '\0', sizeof(network_memb) ); 
                for ( int i = 0; i < num_groups; i++ ) {
                    network_memb[i] = atoi(&(&target_groups[i][0])[7]); //server
                    printf("%d", network_memb[i]); 
                }
            }
            //membership changed
            if ( Is_caused_join_mess( service_type ) ) {
                printf("Due to the JOIN of %s\n", memb_info.changed_member );
                if (trigger == 1) { //client caused this -> recon is not possible
                    printf("\nclient membership trigger");
                } else {
                    printf("\nserver membership trigger");
                     
                    if ( atoi(&memb_info.changed_member[7]) == server_index ) { //me
                        printf("\nIm the new member\n");
                        if (num_groups > 1) //joining into a group that could have already been transfering data 
                            dummy_join(&memb_info);
                    } else {
                        dummy_join(&memb_info);
                    }
                }
                //changed_member has the name of the new/leaving member (if its join,leave or disconnect) -> if it's a NETWORK membership -> changed_memb is blank & multiple vs_sets has all the 'subsets of daemons coming otgether into the new membership"
            } else if( Is_caused_leave_mess( service_type ) ){
                printf("Due to the LEAVE of %s\n", memb_info.changed_member );
                dummy_leave(&memb_info);
            } else if( Is_caused_disconnect_mess( service_type ) ){
                printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
                //if a client disconected, we can ignore it
                //server went down/disconnected within a group
                dummy_leave(&memb_info);
            } else if( Is_caused_network_mess( service_type ) )
            {
                printf("Due to network mess\n");
                dummy_join(&memb_info);    
            }
        
        } else if (Is_transition_mess(   service_type ) ) {
            printf("received TRANSITIONAL membership for group %s\n", sender );
            dummy_leave(&memb_info);
        }else if( Is_caused_leave_mess( service_type ) ){
            printf("received membership message that left group %s\n", sender );
        }else printf("received incorrecty membership message of type 0x%x\n", service_type );
    
    } else printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);

}

/* writes the contents of new_update to the curr servers log file & saves to disc */
static void write_to_log(char *sec_server) //for the second # of the filename (in case its not OUR log file :))
{
    memset(log_filename, '\0', 9); //clears the contents of logfile 
    strncpy(log_filename, &sc, 1);
    strncpy(&log_filename[1], sec_server, 1);
    strcat(log_filename, "LOG.txt");
    if ( ( fw = fopen(log_filename, "a") ) == NULL ) {
        perror("fopen");
        exit(0);
    }
    int ret;
    //check the type of log 
    if (new_update->type == 1) //new email: add all the email info also
        ret = fprintf(fw, "%d %d|%d|%s|%s|%s|%s\n", new_update->update_id.server, new_update->update_id.sequence_num, new_update->type, new_update->email_.to, new_update->email_.subject, new_update->email_.message, new_update->email_.sender); //update_id|type|email contents (since it's a new email, the update_id & mail_id are the same)
    else if (new_update->type == 2 || new_update->type == 3) //read & delete email update: add the mails id and recipient
        ret = fprintf(fw, "%d %d|%d|%d %d|%s\n",new_update->update_id.server, new_update->update_id.sequence_num, new_update->type, new_update->mail_id.server, new_update->mail_id.sequence_num, new_update->email_.to); //update_id|type|email_id|recipient
    else 
        printf("\nwrite to log type ERROR\n");
    if ( ret < 0 ) printf("fprintf error");
    fclose(fw); //push it to the disk
}

/* creates a new window filled with cells and sends it to the client */
static void request_mailbox(char *client)
{
    window new_window;
    memset(&new_window, 0, sizeof(window)); //fill it with 0's
    int sn = 1; 
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

        buff[strcspn(buff,"\n")] = 0; //gets rid of the new line
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
                    printf("\t extract(subject) = %s\n", extract);
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
        char filenameA[MAX_USERNAME+11] = "";
        get_filename(filenameA, client, 2); //2 is for deletes

        char stat = 'u';
        if ( check_status(&new_cell->mail_id, filenameA) == 1 ) { //email has been deleted
            stat = 'd';
        } else { //check if it's read
            get_filename(filenameA, client, 1);//1 is for reads
            if ( check_status(&new_cell->mail_id,filenameA) == 1 )
                stat = 'r';
        }
        sprintf(&new_cell->status, &stat);
        sn++;
    }
    //send the new_window to the client
    print_window(&new_window);
    int ret = SP_multicast(Mbox, AGREED_MESS, client, 0, sizeof(window), (char*)&new_window);
    if ( ret < 0 ) SP_error( ret ); 
}

/* init the updates_window: array of 5 linked lists */
static void updates_window_init()
{
    for ( int i = 0; i < MAX_SERVERS; i++ ) {
        linkedList *curr_list = &updates_window[i];
        memset(&curr_list->sentinel.update, '\0', sizeof(update));
        curr_list->sentinel.nxt = NULL;
    }
}

//returns -1 if something went wrong while inserting new node/update into the linked list
int repo_insert(int index, update* u)
{
    printf("\nrepo_insert\n");
    print_update(u);
    
    node *pnode;
    pnode = malloc(sizeof(node));
    if( pnode == NULL ) return -1;

    //fill the update
    pnode->update.type = u->type;
    pnode->update.update_id.server = u->update_id.server;
    pnode->update.update_id.sequence_num = u->update_id.sequence_num;
    pnode->update.mail_id.server = u->mail_id.server;
    pnode->update.mail_id.sequence_num = u->mail_id.sequence_num;
    
    if(u->type == 1) { //new email
        memcpy(&pnode->update.email_,&u->email_,sizeof(email));
    } else { //read or delete
        strcpy(pnode->update.email_.to, u->email_.to);
    }
    //inserts to the end of the linked list (in order)

    node *ptemp; //pointer to the node we must insert after
    ptemp = &updates_window[index].sentinel; //starts at the sentinel node
    while ( ptemp->nxt != NULL ) {
        ptemp = ptemp->nxt;
    }
    pnode->nxt = ptemp->nxt;
    ptemp->nxt = pnode;
    if(pnode->nxt != NULL) printf("\n\t\tERROR with inserting at end of linked list");
    return 0;
}

//prints the updates in the linked list of updats_window[index]
static void print_repo(int i)
{
    printf("\nPRINTING linkedlist[%d]",i);
    if ( updates_window[i].sentinel.nxt == NULL) return;
    node *ptemp = &updates_window[i].sentinel;
    do {
        ptemp = ptemp->nxt;
        printf("\nupdate_id: <%d,%d>", ptemp->update.update_id.server, ptemp->update.update_id.sequence_num);
        printf(" mail_id: <%d,%d>", ptemp->update.mail_id.server, ptemp->update.mail_id.sequence_num);
        printf(", update type: %d ", ptemp->update.type);
        
        if(ptemp->update.type == 1) { //new email
            print_email(&ptemp->update.email_);
        } else { //read or delete
            printf(" requested by: %s\n", ptemp->update.email_.to);
        }
    } while(ptemp->nxt != NULL);
}

//prints contents of the window
void print_window(window* w)
{
    cell *cell_;
    printf("\tsn#\t<server,seq_num>\tsubject\tmessage\tfrom\n");
    for ( int i = 0; i < MAX_CELLS; i++ ) {
        cell_ = &w->window[i];
        if(cell_->sn == 0) return; //no more to print 
        printf("\n\t%d\t<%d,%d>\t%s\t%s\t%s\t%c", cell_->sn, cell_->mail_id.server, cell_->mail_id.sequence_num, cell_->mail.subject, cell_->mail.message, cell_->mail.sender, cell_->status);
    }
}

/* sends updates with id: server,min_sn until server,max_sn(inclusive) */
static void resend_updates(int server, int min_sn, int max_sn)
{
    printf("\nI(server %d) am resending updates from server %d",server_index, server);
//find node with the update having ID: min_sn
    print_repo(server-1);
    //since we insert at the front of the sentinel -> the first one is the last one kind of...
    node *ptemp = &updates_window[server-1].sentinel; //sentinel node -> now we have to find the update with min_sn

    int want_sn = min_sn; //id sequence_num we are looking for atm
    //find the node holding update with id: min_sn
    printf("\nsearching for want_sn = %d", want_sn);
    while ( ptemp->nxt != NULL && want_sn <= max_sn) {
        ptemp = ptemp->nxt;
        if (ptemp->update.update_id.sequence_num == want_sn ) { 
            printf("found the update with seq_num == %d", want_sn);
            print_id(&ptemp->update.update_id);
            update *ptemp_update = &ptemp->update;
            printf("\n\tptemp_update is now: ");
            print_update(ptemp_update);
            ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 5, sizeof(update), (char*)(ptemp_update));
            want_sn++;
        }
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

/* adds the update to our linked list & updates our status_matrix */
static void apply_update()
{
    //adds new_update to OUR linked list in updates_window
    if ( repo_insert(server_index-1, new_update) == -1)
        printf("error");
    print_repo(server_index-1);
    //updates our status_matrix[our server] = seq num we just sent
    status_matrix[server_index-1][server_index-1] = updates_sent;
    print_matrix();
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

static void dummy_leave(membership_info *mem_info)
{
    printf("\ndummy_leave()\n");
    if (state == 1) { //recon state
        //printf("\nNeed to restart reconciliation");
        recon_num = mem_info->gid.id[2];
        recon(); 
    } else printf("...nothing needs to happen");
    fflush(0);
} 

static void dummy_join(membership_info *mem_info)
{
    printf("\ndummy_join()\n");
    //check state 
    if (state == 0) //normal   
    {
        printf("\tswitching to reconciliation state\n");
        state = 1;
    } else { //already in recon state
        printf("We are already in reconciliation state: \n\tNeed to restart reconciliation");
    }
    recon_num = mem_info->gid.id[2]; //i think this is the unique int at the end per group membership change
    recon();
}

/* prints contents of email m*/
static void print_email(email *m)
{
    printf("to: <%s>|subject: <%s>|message: <%s>|sender: <%s>", m->to, m->subject, m->message, m->sender );
}

/* prints the id in form: <server, sequence_num> */
static void print_id(id *id_)
{
    printf("<%d,%d>", id_->server, id_->sequence_num); 
}

/* prints contents of update */
static void print_update(update *u)
{
    printf("type:%d", u->type);
    printf("|update_id: ");
    print_id(&u->update_id);
    printf("|mail_id: ");
    print_id(&u->mail_id);
    if ( u->type == 1 ) { //new email
       printf("|email contents: ");
       print_email(&u->email_);
    } else { //2 or 3 (read or delete)
        printf("|recipient: %s", u->email_.to);
    }
}

static void print_matrix()
{
    printf("\nstatus matrix:\n");
    for(int r = 0; r < MAX_SERVERS; r++) {
        for(int c = 0; c < MAX_SERVERS; c++) {
            printf(" %d ", status_matrix[r][c]);
        }
        printf("\n");
    }
}

static void recon()
{
    printf("\nreconciliation()");
    matrixStatus my_status;
    my_status.membership = recon_num; //needs to be the current membership id + unique id?(does this change even when one server joins another)
    memcpy(my_status.id_matrix, status_matrix, sizeof(status_matrix)); //copies status_matrix into my_status.id_matrix
    printf("\nRecon unique round/ID: %d\n", recon_num);
    //clear the incoming matrices array
    for ( int i = 0; i < MAX_SERVERS; i++ )
        memset(&recon_matrices[i], '\0', sizeof(matrixStatus));
    
    //multicast our status matrix
    ret = SP_multicast(Mbox, AGREED_MESS, "all_servers", 6, sizeof(matrixStatus), (char*)(&my_status)); //case 6 = recon mode
}

/* when a server crashes and they come back, they have to read all the updates from their logs */
static void reboot_logs()
{
    new_update = malloc(sizeof(update));
    for ( int i = 1; i <= MAX_SERVERS; i++ ) {
        memset(log_filename, '\0', 9); //clears the contents of logfile 
        strncpy(log_filename, &sc, 1);
        char serv = i + '0';
        strncpy(&log_filename[1], &serv, 1); 
        strcat(log_filename, "LOG.txt");
        if ( ( fr = fopen(log_filename, "r") ) == NULL ) //no logs for this server
            continue;

        char buff[256]; //reads everyline into buff 
        int line = 1; //only puts most recent 20 in our linked list
        while (fgets(buff, 256, fr) && line <= 20)
        {
            buff[strcspn(buff,"\n")] = 0; //gets rid of the new line
            char tkn[] = "|"; 
            char *extract = strtok(buff, tkn);
            int round = 0;
            while(extract != NULL)
            {
                switch(round) {
                    case 0: ;
                        //the updates id
                        new_update->update_id.server = atoi(&extract[0]);
                        new_update->update_id.sequence_num = atoi(&extract[2]);
                        break;
                    case 2: ;
                        new_update->type = atoi(&extract[0]);
                        break;
                    case 3: ;
                        //depends on type now
                        if(new_update->type == 1) //new email
                            sprintf(new_update->email_.to, extract); 
                        else { //read or delete -> mail_id
                            new_update->mail_id.server = atoi(&extract[0]);
                            new_update->mail_id.sequence_num = atoi(&extract[2]);
                        }
                        break;
                    case 4: ;
                        if(new_update->type == 1) //new email
                            sprintf(new_update->email_.subject, extract); 
                        else //r or d ->to(requested it)
                            sprintf(new_update->email_.to, extract);
                        break;
                    case 5: ;
                        if(new_update->type == 1) //new email
                            sprintf(new_update->email_.message, extract); 
                        break;
                    case 6: ;
                        if(new_update->type == 1) //new email
                            sprintf(new_update->email_.sender, extract); 
                        break;
                }
                //put it into our linked list & update status matrix
                repo_insert(i - 1, new_update);
                status_matrix[server_index-1][i- 1] = new_update->update_id.sequence_num;

                extract = strtok(NULL, tkn);
                round++;
            }
        }
        print_matrix();
    }
}

/* checks if server is in the current network component, returns 1 if they are, else 0 */
static int in_network(int server)
{
    for ( int i = 0; i < MAX_SERVERS; i++) {
        if ( network_memb[i] == server )
            return 1;
    }
    return 0;
}

static void Bye()
{
    To_exit = 1;
    free(new_update);
    printf("\nBye.\n");
    E_detach_fd( Mbox, READ_FD ); 
    SP_disconnect( Mbox );
    exit(0);
}
