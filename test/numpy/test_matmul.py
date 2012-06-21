import cphvbnumpy as np
from numpytest import numpytest

class test_matmul(numpytest):
    def init(self):
        for t in ['np.float32','np.float64','np.int64']:
            maxdim = 6
            for m in range(1,maxdim+1):
                for n in range(1,maxdim+1):
                    for k in range(1,maxdim+1):
                        A = self.array((m,k))
                        B = self.array((k,n))
                        a = {}
                        cmd  = "a[0] = self.array((%d,%d),dtype=%s);"%(m,k,t)
                        cmd += "a[1] = self.array((%d,%d),dtype=%s);"%(k,n,t)
                        exec cmd
                        yield (a,cmd) 

    def test_matmul(self,a):
        cmd = "a[2] = np.matmul(a[0],a[1])"
        exec cmd
        return (a[2], cmd)

    def test_dot(self,a):
        cmd = "a[2] = np.dot(a[0],a[1])"
        exec cmd
        return (a[2], cmd)
