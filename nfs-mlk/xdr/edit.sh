sed  's/svc_sendreply/svc_sendreply_mlk/g' $1 > /tmp/foo
sed  '110i\
extern int svc_sendreply_mlk(SVCXPRT *svcp, xdrproc_t proc, char *parm);
' /tmp/foo > $1
