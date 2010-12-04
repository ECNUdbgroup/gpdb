/*-------------------------------------------------------------------------
 *
 * gist_private.h
 *	  private declarations for GiST -- declarations related to the
 *	  internal implementation of GiST, not the public API
 *
 * Portions Copyright (c) 1996-2010, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/gist_private.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef GIST_PRIVATE_H
#define GIST_PRIVATE_H

#include "access/gist.h"
#include "access/itup.h"
#include "storage/bufmgr.h"
#include "utils/rbtree.h"

/* Buffer lock modes */
#define GIST_SHARE	BUFFER_LOCK_SHARE
#define GIST_EXCLUSIVE	BUFFER_LOCK_EXCLUSIVE
#define GIST_UNLOCK BUFFER_LOCK_UNLOCK

/*
 * GISTSTATE: information needed for any GiST index operation
 *
 * This struct retains call info for the index's opclass-specific support
 * functions (per index column), plus the index's tuple descriptor.
 */
typedef struct GISTSTATE
{
	FmgrInfo	consistentFn[INDEX_MAX_KEYS];
	FmgrInfo	unionFn[INDEX_MAX_KEYS];
	FmgrInfo	compressFn[INDEX_MAX_KEYS];
	FmgrInfo	decompressFn[INDEX_MAX_KEYS];
	FmgrInfo	penaltyFn[INDEX_MAX_KEYS];
	FmgrInfo	picksplitFn[INDEX_MAX_KEYS];
	FmgrInfo	equalFn[INDEX_MAX_KEYS];
	FmgrInfo	distanceFn[INDEX_MAX_KEYS];

	TupleDesc	tupdesc;
} GISTSTATE;


/*
 * During a GiST index search, we must maintain a queue of unvisited items,
 * which can be either individual heap tuples or whole index pages.  If it
 * is an ordered search, the unvisited items should be visited in distance
 * order.  Unvisited items at the same distance should be visited in
 * depth-first order, that is heap items first, then lower index pages, then
 * upper index pages; this rule avoids doing extra work during a search that
 * ends early due to LIMIT.
 *
 * To perform an ordered search, we use an RBTree to manage the distance-order
 * queue.  Each GISTSearchTreeItem stores all unvisited items of the same
 * distance; they are GISTSearchItems chained together via their next fields.
 *
 * In a non-ordered search (no order-by operators), the RBTree degenerates
 * to a single item, which we use as a queue of unvisited index pages only.
 * In this case matched heap items from the current index leaf page are
 * remembered in GISTScanOpaqueData.pageData[] and returned directly from
 * there, instead of building a separate GISTSearchItem for each one.
 */

/* Individual heap tuple to be visited */
typedef struct GISTSearchHeapItem
{
	ItemPointerData heapPtr;
	bool		recheck;		/* T if quals must be rechecked */
} GISTSearchHeapItem;

/* Unvisited item, either index page or heap tuple */
typedef struct GISTSearchItem
{
	struct GISTSearchItem *next;	/* list link */
	BlockNumber blkno;			/* index page number, or InvalidBlockNumber */
	union
	{
		GistNSN		parentlsn;		/* parent page's LSN, if index page */
		/* we must store parentlsn to detect whether a split occurred */
		GISTSearchHeapItem heap;	/* heap info, if heap tuple */
	}			data;
}	GISTSearchItem;

#define GISTSearchItemIsHeap(item)  ((item).blkno == InvalidBlockNumber)

/*
 * Within a GISTSearchTreeItem's chain, heap items always appear before
 * index-page items, since we want to visit heap items first.  lastHeap points
 * to the last heap item in the chain, or is NULL if there are none.
 */
typedef struct GISTSearchTreeItem
{
	RBNode		rbnode;			/* this is an RBTree item */
	GISTSearchItem *head;		/* first chain member */
	GISTSearchItem *lastHeap;	/* last heap-tuple member, if any */
	double		distances[1];	/* array with numberOfOrderBys entries */
} GISTSearchTreeItem;

#define GSTIHDRSZ offsetof(GISTSearchTreeItem, distances)

/*
 * GISTScanOpaqueData: private state for a scan of a GiST index
 */
typedef struct GISTScanOpaqueData
{
	GISTSTATE  *giststate;		/* index information, see above */
	RBTree	   *queue;			/* queue of unvisited items */
	MemoryContext queueCxt;		/* context holding the queue */
	MemoryContext tempCxt;		/* workspace context for calling functions */
	bool		qual_ok;		/* false if qual can never be satisfied */
	bool		firstCall;		/* true until first gistgettuple call */

	GISTSearchTreeItem *curTreeItem;	/* current queue item, if any */

	/* pre-allocated workspace arrays */
	GISTSearchTreeItem *tmpTreeItem;	/* workspace to pass to rb_insert */
	double	   *distances;		/* output area for gistindex_keytest */

	/* In a non-ordered search, returnable heap items are stored here: */
	GISTSearchHeapItem pageData[BLCKSZ / sizeof(IndexTupleData)];
	OffsetNumber nPageData;		/* number of valid items in array */
	OffsetNumber curPageData;	/* next item to return */
} GISTScanOpaqueData;

typedef GISTScanOpaqueData *GISTScanOpaque;


/* XLog stuff */

#define XLOG_GIST_PAGE_UPDATE		0x00
#define XLOG_GIST_NEW_ROOT			0x20
#define XLOG_GIST_PAGE_SPLIT		0x30
#define XLOG_GIST_INSERT_COMPLETE	0x40
#define XLOG_GIST_CREATE_INDEX		0x50
#define XLOG_GIST_PAGE_DELETE		0x60

typedef struct gistxlogPageUpdate
{
	RelFileNode node;
	BlockNumber blkno;

	/*
	 * It used to identify completeness of insert. Sets to leaf itup
	 */
	ItemPointerData key;

	/* number of deleted offsets */
	uint16		ntodelete;

	/*
	 * follow: 1. todelete OffsetNumbers 2. tuples to insert
	 */
} gistxlogPageUpdate;

typedef struct gistxlogPageSplit
{
	RelFileNode node;
	BlockNumber origblkno;		/* splitted page */
	bool		origleaf;		/* was splitted page a leaf page? */
	uint16		npage;

	/* see comments on gistxlogPageUpdate */
	ItemPointerData key;

	/*
	 * follow: 1. gistxlogPage and array of IndexTupleData per page
	 */
} gistxlogPageSplit;

typedef struct gistxlogPage
{
	BlockNumber blkno;
	int			num;			/* number of index tuples following */
} gistxlogPage;

typedef struct gistxlogInsertComplete
{
	RelFileNode node;
	/* follows ItemPointerData key to clean */
} gistxlogInsertComplete;

typedef struct gistxlogPageDelete
{
	RelFileNode node;
	BlockNumber blkno;
} gistxlogPageDelete;

/* SplitedPageLayout - gistSplit function result */
typedef struct SplitedPageLayout
{
	gistxlogPage block;
	IndexTupleData *list;
	int			lenlist;
	IndexTuple	itup;			/* union key for page */
	Page		page;			/* to operate */
	Buffer		buffer;			/* to write after all proceed */

	struct SplitedPageLayout *next;
} SplitedPageLayout;

/*
 * GISTInsertStack used for locking buffers and transfer arguments during
 * insertion
 */

typedef struct GISTInsertStack
{
	/* current page */
	BlockNumber blkno;
	Buffer		buffer;
	Page		page;

	/*
	 * log sequence number from page->lsn to recognize page update	and
	 * compare it with page's nsn to recognize page split
	 */
	GistNSN		lsn;

	/* child's offset */
	OffsetNumber childoffnum;

	/* pointer to parent and child */
	struct GISTInsertStack *parent;
	struct GISTInsertStack *child;

	/* for gistFindPath */
	struct GISTInsertStack *next;
} GISTInsertStack;

typedef struct GistSplitVector
{
	GIST_SPLITVEC splitVector;	/* to/from PickSplit method */

	Datum		spl_lattr[INDEX_MAX_KEYS];		/* Union of subkeys in
												 * spl_left */
	bool		spl_lisnull[INDEX_MAX_KEYS];
	bool		spl_leftvalid;

	Datum		spl_rattr[INDEX_MAX_KEYS];		/* Union of subkeys in
												 * spl_right */
	bool		spl_risnull[INDEX_MAX_KEYS];
	bool		spl_rightvalid;

	bool	   *spl_equiv;		/* equivalent tuples which can be freely
								 * distributed between left and right pages */
} GistSplitVector;

typedef struct
{
	Relation	r;
	IndexTuple *itup;			/* in/out, points to compressed entry */
	int			ituplen;		/* length of itup */
	Size		freespace;		/* free space to be left */
	GISTInsertStack *stack;
	bool		needInsertComplete;

	/* pointer to heap tuple */
	ItemPointerData key;
} GISTInsertState;

/* root page of a gist index */
#define GIST_ROOT_BLKNO				0

/*
 * mark tuples on inner pages during recovery
 */
#define TUPLE_IS_VALID		0xffff
#define TUPLE_IS_INVALID	0xfffe

#define  GistTupleIsInvalid(itup)	( ItemPointerGetOffsetNumber( &((itup)->t_tid) ) == TUPLE_IS_INVALID )
#define  GistTupleSetValid(itup)	ItemPointerSetOffsetNumber( &((itup)->t_tid), TUPLE_IS_VALID )
#define  GistTupleSetInvalid(itup)	ItemPointerSetOffsetNumber( &((itup)->t_tid), TUPLE_IS_INVALID )

/* gist.c */
extern Datum gistbuild(PG_FUNCTION_ARGS);
extern Datum gistinsert(PG_FUNCTION_ARGS);
extern MemoryContext createTempGistContext(void);
extern void initGISTstate(GISTSTATE *giststate, Relation index);
extern void freeGISTstate(GISTSTATE *giststate);
extern void gistmakedeal(GISTInsertState *state, GISTSTATE *giststate);
extern void gistnewroot(Relation r, Buffer buffer, IndexTuple *itup, int len, ItemPointer key);

extern SplitedPageLayout *gistSplit(Relation r, Page page, IndexTuple *itup,
		  int len, GISTSTATE *giststate);

extern GISTInsertStack *gistFindPath(Relation r, BlockNumber child);

/* gistxlog.c */
extern void gist_redo(XLogRecPtr lsn, XLogRecord *record);
extern void gist_desc(StringInfo buf, uint8 xl_info, char *rec);
extern void gist_xlog_startup(void);
extern void gist_xlog_cleanup(void);
extern bool gist_safe_restartpoint(void);
extern IndexTuple gist_form_invalid_tuple(BlockNumber blkno);

extern XLogRecData *formUpdateRdata(RelFileNode node, Buffer buffer,
				OffsetNumber *todelete, int ntodelete,
				IndexTuple *itup, int ituplen, ItemPointer key);

extern XLogRecData *formSplitRdata(RelFileNode node,
			   BlockNumber blkno, bool page_is_leaf,
			   ItemPointer key, SplitedPageLayout *dist);

extern XLogRecPtr gistxlogInsertCompletion(RelFileNode node, ItemPointerData *keys, int len);

/* gistget.c */
extern Datum gistgettuple(PG_FUNCTION_ARGS);
extern Datum gistgetbitmap(PG_FUNCTION_ARGS);

/* gistutil.c */

#define GiSTPageSize   \
	( BLCKSZ - SizeOfPageHeaderData - MAXALIGN(sizeof(GISTPageOpaqueData)) )

#define GIST_MIN_FILLFACTOR			10
#define GIST_DEFAULT_FILLFACTOR		90

extern Datum gistoptions(PG_FUNCTION_ARGS);
extern bool gistfitpage(IndexTuple *itvec, int len);
extern bool gistnospace(Page page, IndexTuple *itvec, int len, OffsetNumber todelete, Size freespace);
extern void gistcheckpage(Relation rel, Buffer buf);
extern Buffer gistNewBuffer(Relation r);
extern void gistfillbuffer(Page page, IndexTuple *itup, int len,
			   OffsetNumber off);
extern IndexTuple *gistextractpage(Page page, int *len /* out */ );
extern IndexTuple *gistjoinvector(
			   IndexTuple *itvec, int *len,
			   IndexTuple *additvec, int addlen);
extern IndexTupleData *gistfillitupvec(IndexTuple *vec, int veclen, int *memlen);

extern IndexTuple gistunion(Relation r, IndexTuple *itvec,
		  int len, GISTSTATE *giststate);
extern IndexTuple gistgetadjusted(Relation r,
				IndexTuple oldtup,
				IndexTuple addtup,
				GISTSTATE *giststate);
extern IndexTuple gistFormTuple(GISTSTATE *giststate,
			  Relation r, Datum *attdata, bool *isnull, bool newValues);

extern OffsetNumber gistchoose(Relation r, Page p,
		   IndexTuple it,
		   GISTSTATE *giststate);
extern void gistcentryinit(GISTSTATE *giststate, int nkey,
			   GISTENTRY *e, Datum k,
			   Relation r, Page pg,
			   OffsetNumber o, bool l, bool isNull);

extern void GISTInitBuffer(Buffer b, uint32 f);
extern void gistdentryinit(GISTSTATE *giststate, int nkey, GISTENTRY *e,
			   Datum k, Relation r, Page pg, OffsetNumber o,
			   bool l, bool isNull);

extern float gistpenalty(GISTSTATE *giststate, int attno,
			GISTENTRY *key1, bool isNull1,
			GISTENTRY *key2, bool isNull2);
extern bool gistMakeUnionItVec(GISTSTATE *giststate, IndexTuple *itvec, int len, int startkey,
				   Datum *attr, bool *isnull);
extern bool gistKeyIsEQ(GISTSTATE *giststate, int attno, Datum a, Datum b);
extern void gistDeCompressAtt(GISTSTATE *giststate, Relation r, IndexTuple tuple, Page p,
				  OffsetNumber o, GISTENTRY *attdata, bool *isnull);

extern void gistMakeUnionKey(GISTSTATE *giststate, int attno,
				 GISTENTRY *entry1, bool isnull1,
				 GISTENTRY *entry2, bool isnull2,
				 Datum *dst, bool *dstisnull);

extern XLogRecPtr GetXLogRecPtrForTemp(void);

/* gistvacuum.c */
extern Datum gistbulkdelete(PG_FUNCTION_ARGS);
extern Datum gistvacuumcleanup(PG_FUNCTION_ARGS);

/* gistsplit.c */
extern void gistSplitByKey(Relation r, Page page, IndexTuple *itup,
			   int len, GISTSTATE *giststate,
			   GistSplitVector *v, GistEntryVector *entryvec,
			   int attno);

#endif   /* GIST_PRIVATE_H */
