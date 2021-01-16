/***************** Transaction class **********************/
/*** Implements methods that handle Begin, Read, Write, ***/
/*** Abort, Commit operations of transactions. These    ***/
/*** methods are passed as parameters to threads        ***/
/*** spawned by Transaction manager class.              ***/
/**********************************************************/

/* Required header files */
#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include "zgt_def.h"
#include "zgt_tm.h"
#include "zgt_extern.h"
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <pthread.h>

//Modified at 6:35 PM 09/29/2014 by Jay D. Bodra. Search for "Fall 2014" to see the changes
// Fall 2014[jay]. Removed the TxType that was provided. Now is it initialized once in the constructor

extern void *start_operation(long, long);  //start an op with mutex lock and cond wait
extern void *finish_operation(long);        //finish an op with mutex unlock and con signal

extern void *do_commit_abort(long, char);   //commit/abort based on char value
extern void *process_read_write(long, long, int, char);

extern zgt_tm *ZGT_Sh;			// Transaction manager object

/* Transaction class constructor */
/* Initializes transaction id and status and thread id */
/* Input: Transaction id, status, thread id */

//Fall 2014[jay]. Modified zgt_tx() in zgt_tx.h
zgt_tx::zgt_tx( long tid, char Txstatus,char type, pthread_t thrid){
  this->lockmode = (char)' ';  //default
  this->Txtype = type; //Fall 2014[jay] R = read only, W=Read/Write
  this->sgno =1;
  this->tid = tid;
  this->obno = -1; //set it to a invalid value
  this->status = Txstatus;
  this->pid = thrid;
  this->head = NULL;
  this->nextr = NULL;
  this->semno = -1; //init to  an invalid sem value
}

/* Method used to obtain reference to a transaction node      */
/* Inputs the transaction id. Makes a linear scan over the    */
/* linked list of transaction nodes and returns the reference */
/* of the required node if found. Otherwise returns NULL      */

zgt_tx* get_tx(long tid1){  
  zgt_tx *txptr, *lastr1;
  
  if(ZGT_Sh->lastr != NULL){	// If the list is not empty
      lastr1 = ZGT_Sh->lastr;	// Initialize lastr1 to first node's ptr
      for  (txptr = lastr1; (txptr != NULL); txptr = txptr->nextr)
	    if (txptr->tid == tid1) 		// if required id is found									
	       return txptr; 
      return (NULL);			// if not found in list return NULL
   }
  return(NULL);				// if list is empty return NULL
}

/* Method that handles "BeginTx tid" in test file     */
/* Inputs a pointer to transaction id, obj pair as a struct. Creates a new  */
/* transaction node, initializes its data members and */
/* adds it to transaction list */

void *begintx(void *arg){
  //intialise a transaction object. Make sure it is 
  //done after acquiring the semaphore for the tm and making sure that 
  //the operation can proceed using the condition variable. when creating
  //the tx object, set the tx to TR_ACTIVE and obno to -1; there is no 
  //semno as yet as none is waiting on this tx.
  
  struct param *node = (struct param*)arg;// get tid and count
  start_operation(node->tid, node->count); 
    zgt_tx *tx = new zgt_tx(node->tid,TR_ACTIVE, node->Txtype, pthread_self());	// Create new tx node
 
    //Fall 2014[jay]. writes the Txtype to the file.
  
    zgt_p(0);				// Lock Tx manager; Add node to transaction list
  
    tx->nextr = ZGT_Sh->lastr;
    ZGT_Sh->lastr = tx;   
    zgt_v(0); 			// Release tx manager 
  fprintf(ZGT_Sh->logfile, "T%d\t%c \tBeginTx\n", node->tid, node->Txtype);	// Write log record and close
    fflush(ZGT_Sh->logfile);
  finish_operation(node->tid);
  pthread_exit(NULL);				// thread exit
}

/* Method to handle Readtx action in test file    */
/* Inputs a pointer to structure that contans     */
/* tx id and object no to read. Reads the object  */
/* if the object is not yet present in hash table */
/* or same tx holds a lock on it. Otherwise waits */
/* until the lock is released */

void *readtx(void *arg){
  struct param *node = (struct param*)arg;// get tid and objno and count 
  start_operation(node->tid, node->count);  
  process_read_write(node->tid, node->obno, node->count, 'S');
  finish_operation(node->tid);
}


void *writetx(void *arg){ //do the operations for writing; similar to readTx
  struct param *node = (struct param*)arg; // struct parameter that contains 
  start_operation(node->tid, node->count);  
  process_read_write(node->tid, node->obno, node->count, 'X');
  finish_operation(node->tid);
  //do the operations for writing; similar to readTx. Write your code

}

//common method to process read/write: just a suggestion

void *process_read_write(long tid, long obno,  int count, char mode){
  zgt_tx *tx = get_tx(tid);
  // zgt_v(0);
  // cout<<"releasing the hold prw ignore just call to setlock of tm "<<tid<<" "<<obno<<"\n";
  int lock_permission = tx->set_lock(tid, 1, obno,count, mode);
  if(lock_permission==0)
  {
    cout<<"read done "<<tid<<" "<<obno<<"\n";
    // usleep(ZGT_Sh->optime[tid]);
    tx->perform_readWrite(tid, obno, mode);
  }
  else
  {
    cout<<"in else part\n";
  }
  
}

void *aborttx(void *arg)
{
  struct param *node = (struct param*)arg;// get tid and count  

  start_operation(node->tid, node->count);  

  // cout<<"abort started\n"; 
  zgt_tx *tx = get_tx(node->tid);
  tx->status = TR_ABORT;
  tx->free_locks();
  tx->remove_tx();  
  zgt_v(node->tid); 
  finish_operation(node->tid); 
  // cout<<"Abort "<<node->tid<<"done \n";
  fprintf(ZGT_Sh->logfile, "T%d\t\t%c \tAbort\n", node->tid, node->Txtype);
  fflush(ZGT_Sh->logfile);  
  pthread_exit(NULL);   	// thread exit
}

void *committx(void *arg)
{
 
    //remove the locks/objects before committing
  struct param *node = (struct param*)arg;// get tid and count   
  start_operation(node->tid, node->count); 
  
  // cout<<"commit started "<<node->tid<<"\n";
  // zgt_p(0);
  // cout<<"inside the black hole\n";
  zgt_tx *tx = get_tx(node->tid);
  tx->status = TR_END;
  tx->free_locks();
  tx->remove_tx();  
  // zgt_v(0);
  // cout<<"giving up on tm lock";
  zgt_v(node->tid); 
  finish_operation(node->tid); 
  // cout<<"Commit "<<node->tid<<" done\n";
  fprintf(ZGT_Sh->logfile, "T%d\tCommit\n", node->tid);
  fflush(ZGT_Sh->logfile);
  pthread_exit(NULL);	// thread exit
}

//suggestion as they are very similar

// called from commit/abort with appropriate parameter to do the actual
// operation. Make sure you give error messages if you are trying to
// commit/abort a non-existant tx

void *do_commit_abort(long t, char status){

  // write your code
}

int zgt_tx::remove_tx ()
{
  //remove the transaction from the TM
  
  zgt_tx *txptr, *lastr1;
  lastr1 = ZGT_Sh->lastr;
  for(txptr = ZGT_Sh->lastr; txptr != NULL; txptr = txptr->nextr){	// scan through list
	  if (txptr->tid == this->tid){		// if correct node is found          
		 lastr1->nextr = txptr->nextr;	// update nextr value; done
		 //delete this;
         return(0);
	  }
	  else lastr1 = txptr->nextr;			// else update prev value
   }
  fprintf(ZGT_Sh->logfile, "Trying to Remove a Tx:%d that does not exist\n", this->tid);
  fflush(ZGT_Sh->logfile);
  printf("Trying to Remove a Tx:%d that does not exist\n", this->tid);
  fflush(stdout);
  return(-1);
}

/* this method sets lock on objno1 with lockmode1 for a tx*/

int zgt_tx::set_lock(long tid1, long sgno1, long obno1, int count, char lockmode1){
  //if the thread has to wait, block the thread on a semaphore from the
  //sempool in the transaction manager. Set the appropriate parameters in the
  //transaction list if waiting.
  //if successful  return(0); else -1
  
    //write your code
  zgt_tx *tx = get_tx(tid1);
  // zgt_v(0);
  // cout<<"releasing the hold of tm "<<tid1<<" "<<obno1<<"\n";
  zgt_hlink *object = ZGT_Ht->find(sgno1, obno1);
  if(object == NULL)
  { 
    // cout<<"setting up a lock object not present in hash table\n";
    ZGT_Ht->add(tx, sgno1, obno1, lockmode1);
    return (0);
  } 
  else
  {
    // cout<<"trying to hold the tm for owner object "<<obno1<<"\n";
    // zgt_p(0); 
    // cout<<"holding the tm for owner object "<<obno1<<"\n";
    zgt_tx *holder_tx = get_tx(object->tid);
    // zgt_v(0);
    // cout<<"trying to hold the tm for owner object "<<holder_tx->tid<<" "<<obno1<<"\n";
    // cout<<holder_tx->Txtype<<" holder and  requesting tx "<<tx->Txtype<<"\n";
    if(object->tid == tid1)
    {
      // cout<<"same owner\n";
      return (0);
    }
    else if(holder_tx->Txtype == 'R' && tx->Txtype == 'R')
    {
      // cout<<"allowed because of read read\n";
      ZGT_Ht->add(tx, sgno1, obno1, lockmode1);
      return (0);
    }
    else
    {  
      tx->setTx_semno(holder_tx->tid, holder_tx->tid);
      tx->status = TR_WAIT;
      zgt_p(holder_tx->tid);
      zgt_v(holder_tx->tid); 
      tx->status = TR_ACTIVE;
      ZGT_Ht->add(tx, sgno1, obno1, lockmode1);
      return (0);
    }
  }
  
}

int zgt_tx::free_locks()
{
  
  // this part frees all locks owned by the transaction
  // that is, remove the objects from the hash table
  // and release all Tx's waiting on this Tx

  zgt_hlink* temp = head;  //first obj of tx
  
  for(temp;temp != NULL;temp = temp->nextp){	// SCAN Tx obj list

      fprintf(ZGT_Sh->logfile, "%d : %d, ", temp->obno, ZGT_Sh->objarray[temp->obno]->value);
      fflush(ZGT_Sh->logfile);
      
      if (ZGT_Ht->remove(this,1,(long)temp->obno) == 1){
	   printf(":::ERROR:node with tid:%d and onjno:%d was not found for deleting", this->tid, temp->obno);		// Release from hash table
	   fflush(stdout);
      }
      else {
#ifdef TX_DEBUG
	   printf("\n:::Hash node with Tid:%d, obno:%d lockmode:%c removed\n",
                            temp->tid, temp->obno, temp->lockmode);
	   fflush(stdout);
#endif
      }
    }
  fprintf(ZGT_Sh->logfile, "\n");
  fflush(ZGT_Sh->logfile);
  
  return(0);
}		

// CURRENTLY Not USED
// USED to COMMIT
// remove the transaction and free all associate dobjects. For the time being
// this can be used for commit of the transaction.

int zgt_tx::end_tx()  //2014: not used
{
  zgt_tx *linktx, *prevp;
  
  // USED to COMMIT 
  //remove the transaction and free all associate dobjects. For the time being 
  //this can be used for commit of the transaction.
  
  linktx = prevp = ZGT_Sh->lastr;
  
  while (linktx){
    if (linktx->tid  == this->tid) break;
    prevp  = linktx;
    linktx = linktx->nextr;
  }
  if (linktx == NULL) {
    printf("\ncannot remove a Tx node; error\n");
    fflush(stdout);
    return (1);
  }
  if (linktx == ZGT_Sh->lastr) ZGT_Sh->lastr = linktx->nextr;
  else {
    prevp = ZGT_Sh->lastr;
    while (prevp->nextr != linktx) prevp = prevp->nextr;
    prevp->nextr = linktx->nextr;    
  }
}

// currently not used
int zgt_tx::cleanup()
{
  return(0);
  
}

// routine to print the tx list
// TX_DEBUG should be defined in the Makefile to print
void zgt_tx::print_tm(){
  
  zgt_tx *txptr;
  
#ifdef TX_DEBUG
  printf("printing the tx  list \n");
  printf("Tid\tTxType\tThrid\t\tobjno\tlock\tstatus\tsemno\n");
  fflush(stdout);
#endif
  txptr=ZGT_Sh->lastr;
  while (txptr != NULL) {
#ifdef TX_DEBUG
    printf("%d\t%c\t%d\t%d\t%c\t%c\t%d\n", txptr->tid, txptr->Txtype, txptr->pid, txptr->obno, txptr->lockmode, txptr->status, txptr->semno);
    fflush(stdout);
#endif
    txptr = txptr->nextr;
  }
  fflush(stdout);
}

//need to be called for printing
void zgt_tx::print_wait(){

  //route for printing for debugging
  
  printf("\n    SGNO        TxType       OBNO        TID        PID         SEMNO   L\n");
  printf("\n");
}

void zgt_tx::print_lock(){
  //routine for printing for debugging
  
  printf("\n    SGNO        OBNO        TID        PID   L\n");
  printf("\n");
  
}

// routine to perform the acutual read/write operation
// based  on the lockmode

void zgt_tx::perform_readWrite(long tid,long obno, char lockmode){
  
 if(lockmode=='X')
  {
    ZGT_Sh->objarray[obno]->value++;
    fprintf(ZGT_Sh->logfile, "T%d\t\t\tWriteTx\t%d:%d:%d\tWriteLock\tGranted\tP\n", tid,obno, ZGT_Sh->objarray[obno]->value,ZGT_Sh->optime[tid]);
    fflush(ZGT_Sh->logfile);
  }
  else
  {
    ZGT_Sh->objarray[obno]->value--;
    fprintf(ZGT_Sh->logfile, "T%d\t\t\tReadTx\t%d:%d:%d\tReadLock\tGranted\tP\n", tid,obno, ZGT_Sh->objarray[obno]->value, ZGT_Sh->optime[tid]);
    fflush(ZGT_Sh->logfile);
  }


}

// routine that sets the semno in the Tx when another tx waits on it.
// the same number is the same as the tx number on which a Tx is waiting
int zgt_tx::setTx_semno(long tid, int semno){
  zgt_tx *txptr;
  
  txptr = get_tx(tid);
  if (txptr == NULL){
    printf("\n:::ERROR:Txid %d wants to wait on sem:%d of tid:%d which does not exist\n", this->tid, semno, tid);
    fflush(stdout);
    exit(1);
  }
  if ((txptr->semno == -1)|| (txptr->semno == semno)){  //just to be safe
    txptr->semno = semno;
    return(0);
  }
  else if (txptr->semno != semno){
#ifdef TX_DEBUG
    printf(":::ERROR Trying to wait on sem:%d, but on Tx:%d\n", semno, txptr->tid);
    fflush(stdout);
#endif
    exit(1);
  }
  return(0);
}

void *start_operation(long tid, long count){
  
  pthread_mutex_lock(&ZGT_Sh->mutexpool[tid]);	// Lock mutex[t] to make other
  // threads of same transaction to wait
  
  while(ZGT_Sh->condset[tid] != count)		// wait if condset[t] is != count
    pthread_cond_wait(&ZGT_Sh->condpool[tid],&ZGT_Sh->mutexpool[tid]);
  
}

// Otherside of teh start operation;
// signals the conditional broadcast

void *finish_operation(long tid){
  ZGT_Sh->condset[tid]--;	// decr condset[tid] for allowing the next op
  pthread_cond_broadcast(&ZGT_Sh->condpool[tid]);// other waiting threads of same tx
  pthread_mutex_unlock(&ZGT_Sh->mutexpool[tid]); 
}


