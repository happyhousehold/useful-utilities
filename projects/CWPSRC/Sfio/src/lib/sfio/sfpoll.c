/* Copyright (c) Colorado School of Mines, 2007.*/
/* All rights reserved.                       */

#include	"sfhdr.h"

/*	Poll a set of streams to see if any is available for I/O.
**	Ready streams are moved to front of array but retain the
**	same relative order.
**
**	Written by Kiem-Phong Vo.
*/

#if __STD_C
int sfpoll(Sfio_t** fa, reg int n, int tm)
#else
int sfpoll(fa, n, tm)
Sfio_t**	fa;	/* array of streams to poll		*/
reg int		n;	/* number of streams in array		*/
int		tm;	/* time in millisecs for select/poll	*/
#endif
{
	reg int		r, c, m, np;
	reg Sfio_t*	f;
/* 	reg int		*status, *check; */
	size_t		*status, *check;

	if(n <= 0 || !fa)
		return -1;

/* sfpoll.c:30: warning: assignment from incompatible pointer type */
/* 	if(!(status = (int*)malloc(2*n*sizeof(int))) ) */
	if(!(status = (size_t*)malloc(2*n*sizeof(size_t))) )
		return -1;
	check = status+n; /* streams that need polling */

	/* a SF_READ stream is ready if there is buffered read data */
#define RDREADY(f)	(((f->mode&SF_READ) && f->next < f->endb) || \
			 ((f->mode&SF_WRITE) && f->proc && f->proc->ndata > 0) )

	/* a SF_WRITE stream is ready if there is no write data */
#define WRREADY(f)	(!(f->mode&SF_WRITE) || f->next == f->data)

#define HASAUXFD(f)	(f->proc && f->proc->file >= 0 && f->proc->file != f->file)

	for(r = c = 0; r < n; ++r) /* compute streams that must be checked */
	{	f = fa[r];
		status[r] = 0;

		/* check accessibility */
		m = f->mode&SF_RDWR;
		if((int)f->mode != m && _sfmode(f,m,0) < 0)
			continue;

		if((f->flags&SF_READ) && RDREADY(f))
			status[r] |= SF_READ;

		if((f->flags&SF_WRITE) && WRREADY(f))
			status[r] |= SF_WRITE;

		if((f->flags&SF_RDWR) == status[r])
			continue;

		/* has discipline, ask its opinion */
		if(f->disc && f->disc->exceptf)
		{	if((m = (*f->disc->exceptf)(f,SF_DPOLL,&tm,f->disc)) < 0)
				continue;
			else if(m > 0)
			{	status[r] = m&SF_RDWR;
				continue;
			}
		}

		if(f->extent < 0) /* unseekable stream, must poll/select */
			check[c++] = r;
		else /* seekable streams are always ready */
		{	if(f->flags&SF_READ)
				status[r] |= SF_READ;
			if(f->flags&SF_WRITE)
				status[r] |= SF_WRITE;
		}
	}

	np = -1;
#if _lib_poll
	if(c > 0)
	{	struct pollfd*	fds;

		/* construct the poll array */
		for(m = 0, r = 0; r < c; ++r, ++m)
		{	f = fa[check[r]];
			if(HASAUXFD(f))
				m += 1;
		}
		if(!(fds = (struct pollfd*)malloc(m*sizeof(struct pollfd))) )
			return -1;

		for(m = 0, r = 0; r < c; ++r, ++m)
		{	f = fa[check[r]];

			fds[m].fd = f->file;
			fds[m].events = fds[m].revents = 0;

			if((f->flags&SF_WRITE) && !WRREADY(f) )
				fds[m].events |= POLLOUT;

			if((f->flags&SF_READ)  && !RDREADY(f) )
			{	/* a sfpopen situation with two file descriptors */
				if((f->mode&SF_WRITE) && HASAUXFD(f))
				{	m += 1;
					fds[m].fd = f->proc->file;
					fds[m].revents = 0;
				}

				fds[m].events |= POLLIN;
			}
		}

		while((np = SFPOLL(fds,m,tm)) < 0 )
		{	if(errno == EINTR || errno == EAGAIN)
				errno = 0;
			else	break;
		}
		if(np > 0) /* poll succeeded */
			np = c;

		for(m = 0, r = 0; r < np; ++r, ++m)
		{	f = fa[check[r]];

			if((f->flags&SF_WRITE) && !WRREADY(f) )
			{	if(fds[m].revents&POLLOUT)
					status[check[r]] |= SF_WRITE;
			}

			if((f->flags&SF_READ)  && !RDREADY(f))
			{	if((f->mode&SF_WRITE) && HASAUXFD(f))
					m += 1;
				if(fds[m].revents&POLLIN)
					status[check[r]] |= SF_READ;
			}
		}

		free((Void_t*)fds);
	}
#endif /*_lib_poll*/

#if _lib_select
	if(np < 0 && c > 0)
	{	fd_set		rd, wr;
		struct timeval	tmb, *tmp;

		FD_ZERO(&rd);
		FD_ZERO(&wr);
		m = 0;
		for(r = 0; r < c; ++r)
		{	f = fa[check[r]];

			if(f->file > m)
				m = f->file;

			if((f->flags&SF_WRITE) && !WRREADY(f))
				FD_SET(f->file,&wr);

			if((f->flags&SF_READ)  && !RDREADY(f))
			{	if((f->mode&SF_WRITE) && HASAUXFD(f))
				{	if(f->proc->file > m)
						m = f->proc->file;
					FD_SET(f->proc->file, &rd);
				}
				else	FD_SET(f->file,&rd);
			}
		}
		if(tm < 0)
			tmp = NIL(struct timeval*);
		else
		{	tmp = &tmb;
			tmb.tv_sec = tm/SECOND;
			tmb.tv_usec = (tm%SECOND)*SECOND;
		}

		while((np = select(m+1,&rd,&wr,NIL(fd_set*),tmp)) < 0 )
		{	if(errno == EINTR)
				errno = 0;
			else	break;
		}
		if(np > 0)
			np = c;

		for(r = 0; r < np; ++r)
		{	f = fa[check[r]];

			if((f->flags&SF_WRITE) && !WRREADY(f) )
			{	if(FD_ISSET(f->file,&wr) )
					status[check[r]] |= SF_WRITE;
			}

			if((f->flags&SF_READ) && !RDREADY(f) )
			{	if((f->mode&SF_WRITE) && HASAUXFD(f) )
				{	if(FD_ISSET(f->proc->file, &rd) )
						status[check[r]] |= SF_READ;
				}
				else
				{	if(FD_ISSET(f->file,&rd) )
						status[check[r]] |= SF_READ;
				}
			}
		}
	}
#endif /*_lib_select*/

	for(r = c = 0; c < n; ++c)
	{	if(status[c] == 0)
			continue;

		f = fa[c];
		f->val = (ssize_t)status[c];

		/* announce status */
		if(f->disc && f->disc->exceptf)
/* sfpoll.c:216: warning: cast to pointer from integer of different size */
			(*f->disc->exceptf)(f,SF_READY,(Void_t*)status[c],f->disc);

		if(c > r) /* move to front of list */
		{	fa[c] = fa[r];
			fa[r] = f;
		}
		r += 1;
	}

	free((Void_t*)status);
	return r;
}
