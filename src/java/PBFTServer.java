import java.net.*;
import java.io.*;
import java.util.*;
import java.sql.*;
import javax.sql.rowset.*;
import com.sun.rowset.*;
import java.security.*;
import java.math.*;
import snaq.db.*;

/**
 * 
 * @author knezevic
 *
 * This Java server has two connections.
 * On first connection, it receives PBFT messages, and never sends anything back
 * On the second connection, it receives read-only requests from the clients, and sends the answers
 * generated for both type of requests (those coming from PBFT channel, and direct requests.
 * Requests coming from in should be serialized strings.
 * Responses are serialized CachedRows
 */
public class PBFTServer
{
	private static ResourceBundle configuration = null;
	  
	private ConnectionPool pool;
    private PBFTConnection conn;
    
    private Map<Integer, Socket> clients;
    
    private Boolean[] wsynchro;
    private Boolean[] rsynchro;
	
    /**
     * Returns the value corresponding to a property in the .properties file.
     *
     * @param property the property name
     * @return a <code>String</code> value
     */
    protected String getProperty(String property)
    {
    	try {
    		String s = configuration.getString(property);
    		return s;
    	} catch (MissingResourceException e) {
    		return null;
    	}
    }
    
    // Constructor.
    ///////////////
    public PBFTServer(String filename)
    {
	    // Get and check database.properties
	    System.out.println("Looking for "+filename+".properties in classpath ("+System.getProperty("java.class.path",".")+")");
	    try
	    {
		    configuration = ResourceBundle.getBundle(filename);
	    }
	    catch (java.util.MissingResourceException e)
	    {
		    System.err.println("No "+filename+".properties file has been found in your classpath.<p>");
		    Runtime.getRuntime().exit(1);
	    }

	    // setting up the db connections pool
	    try {
		    Class c = Class.forName(getProperty("db.driver"));

		    Driver driver = (Driver)c.newInstance();

		    DriverManager.registerDriver(driver);

		    pool = new ConnectionPool("rubis",
				    new Integer(getProperty("client.max_clients")),
				    0,
				    180000,
				    getProperty("db.url"),
				    getProperty("db.user"),
				    getProperty("db.pass"));
			pool.getConnection();
	    }
	    catch (ClassNotFoundException c)
	    {
		    System.out.println("Couldn't load database driver: " + c);
		    return;
	    }
        	catch (SQLException sqle)
        	{
        		// ...deal with exception...
			System.out.println("Got exception: "+sqle);
			System.out.println("Got exception: "+sqle.getCause()+", "+sqle.getSQLState());
		    sqle.printStackTrace();
return;

        	}
 	    catch (Exception e)
	    {
		    System.out.println("Problem: " + e);
		    return;
	    }

	    // set up the connection for receiving messages from pbft
	    int maxConnections = new Integer(getProperty("client.max_clients"));
	    int port = 9968;
	    if (getProperty("server.pbft.bind_port")!=null)
		    port = new Integer(getProperty("server.pbft.bind_port"));
	    String bindAddr = getProperty("server.pbft.bind_addr");
	    InetAddress addr;
	    try {
		    addr = InetAddress.getByName(bindAddr);
	    } catch (UnknownHostException e1) {
		    addr = null;
		    System.out.println("Couldn't get the address of " + bindAddr);
	    }

	    // set up client connections
	    rsynchro = new Boolean[maxConnections];
	    wsynchro = new Boolean[maxConnections];
	    for (int i=0; i < maxConnections; i++) {
		    rsynchro[i] = new Boolean(false);
		    wsynchro[i] = new Boolean(false);
	    }

	    clients = new HashMap<Integer, Socket>();
	    // start listening for read-only...
	    new ClientListener().start();

	    // start listening on PBFT connection
	    conn = new PBFTConnection(maxConnections);
	    try
	    {
		    InetSocketAddress sockaddr = new InetSocketAddress(addr, port);
		    DatagramSocket listenerSocket = new DatagramSocket(sockaddr);
		    conn.setSocket(addr, port, listenerSocket);
		    System.out.println("PBFT server set to listen on " + addr + ", port " + port);
		    System.out.println("PBFT server thinks: address=" + listenerSocket.getLocalAddress()+", port="+listenerSocket.getLocalPort());
	    } catch (IOException ioe) {
		    System.out.println("IOException on socket listen: " + ioe);
		    ioe.printStackTrace();
	    } catch (Exception e) {
		    // TODO Auto-generated catch block
		    e.printStackTrace();
	    }

	    PBFTListener pbftListener = new PBFTListener(conn);

	    pbftListener.start();
	    //pbftListener.signal = true;
	    //pool.release();
    }
    
    public static final void main(String[] args)
    {
        String propertiesFileName;
        if (args.length == 0)
            propertiesFileName = "pbft_java_server";
          else
            propertiesFileName = args[0];
        
        PBFTServer server = new PBFTServer(propertiesFileName);
       
/*
	try {
	    previous_socket_timeout = connection.socket.getSoTimeout();
	    connection.socket.setSoTimeout(30000);
	}
	catch( SocketException e ) {
	    // just ignore for now 
	    System.out.println("socket error setting timeout" + e.toString() );
	}

        try {
            String sqlcommand = "SELECT count(*) FROM BIDS";
            connection.send(sqlcommand.getBytes("ISO8859_1"));
            byte[] response = connection.receive();

            System.out.println("response is : " + response);
        } catch (Exception e) {
            System.out.println("Exception caught: " + e.toString() );
            e.printStackTrace();
        }
        */
    }
    
    private class ClientListener extends Thread
    {
    	public boolean signal;
    	public ClientListener() {
    		this.signal = false;
    	}

    	// we're just going to listen on new connections here,
    	// and start worker threads
    	public void run() {
    		int port = 9967;
    		if (getProperty("server.java.bind_port")!=null)
    			port = new Integer(getProperty("server.java.bind_port"));
    		String bindAddr = getProperty("server.java.bind_addr");
    		InetAddress addr;
    		try {
    			addr = InetAddress.getByName(bindAddr);
    		} catch (UnknownHostException e1) {
    			addr = null;
    			System.out.println("Couldn't get the address of " + bindAddr);
    		}
    		ServerSocket serverSocket;
    		try {
    			serverSocket = new ServerSocket();
    			serverSocket.bind(new InetSocketAddress(addr, port));
			System.out.println("Listening on " + addr + ", port " + port);
    		} catch (IOException e) {
    			System.out.println("Couldn't bind to "+addr+":"+port);
    			return;
    		}
    		
    		while (true) {
    			if (signal == true) {
    				return;
    			}
    			try
    			{
    				Socket clientSocket = serverSocket.accept();

				System.out.println("Got connection...");
    				// XXX: first thing a client sends is its id, so use it to initialize entry in the clients list
    				ObjectInputStream ois = new ObjectInputStream(clientSocket.getInputStream());
    				Integer cid = (Integer) ois.readObject();
    				clients.put(cid, clientSocket);
				System.out.println("Generating processor for client id("+cid+")");
    				new ClientProcessor(clientSocket, cid).start();
    			} catch (IOException e) {
    				System.out.println("Error accepting socket..." + e);
    			} catch (ClassNotFoundException e) {
    				System.out.println("Error decoding client's message" + e);
    			}
    		}
    	}
    }
    
    // responsible only for read-only requests.
    private class ClientProcessor extends Thread {
    	private int cid;
    	private Socket socket;
    	private final static int timeout = 2000;
    	public boolean signal;
    	
    	public ClientProcessor(Socket socket, int cid) {
    		this.socket = socket;
    		this.cid = cid;
    		this.signal = false;
    	}
    	
    	private void execute_sql(Connection dbcon, Serializable command) throws Exception
        {
        	Statement stmt = dbcon.createStatement(ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_READ_ONLY);
        	CachedRowSetImpl crset = new CachedRowSetImpl();

		System.out.println("Executing query for client "+cid);
        	ResultSet srs = stmt.executeQuery((String)command);
        	crset.populate(srs);
        	srs.close();
        	stmt.close();

        	// Get the bytes of the serialized object
        	/*
                byte[] buf = bos.toByteArray();
                MessageDigest m=MessageDigest.getInstance("MD5");
                m.update(buf,0,buf.length);
        	 */
        	// and now send the packet back to the client
        	// NOTE: it needs to send client's id first, then the response.
        	synchronized (wsynchro[cid]) {
        		try {
        			ObjectOutput out = new ObjectOutputStream(socket.getOutputStream());
        			out.writeObject(cid);
        			out.writeObject(crset);
        			out.close();
        		} catch (IOException e) {
        		}
        	}
        }
    	
    	public void run() {
		System.out.println("Processor for client " + cid + " is running");
    		while (true) {
    			if (signal) {
    				break;
    			}
    			try
    			{
    				// Read a message sent by client application
    				ObjectInputStream ois = new ObjectInputStream(socket.getInputStream());
				Integer clientId = (Integer) ois.readObject();
    				String data = (String) ois.readObject();

    				Connection con = null;
    				// get the connection to the db and execute it
    				// execute_sql will send the response...
    				try
    				{
					System.out.println("Trying to get a connection...");
    					con = pool.getConnection(timeout);
					System.out.println("Tried to get a connection: "+con);
    					if (con != null)
    					{
    						// ...use the connection...
    						execute_sql(con, data);
    					}
    					else
    					{
    						// ...do something else (timeout occurred)...
    					}
    				}
    				catch (SQLException sqle)
    				{
    					// ...deal with exception...
					System.out.println("SQL exception: "+sqle);
    				}
    				catch (Exception e)
    				{
					System.out.println("Unknown exception: "+e);

    				}
    				finally
    				{
    					try {
    						if (con != null)
    							con.close();
    					}
    					catch (SQLException e) { /* ... */ }
    				}
    				//
    				// Send a response information to the client application
    				//
    				ObjectOutputStream oos = new ObjectOutputStream(socket.getOutputStream());
    				oos.writeObject("Hi...");

    				ois.close();
    				//oos.close();

    				System.out.println("Waiting for client message...");
    			} catch (IOException e) {
				System.out.println("Exception caught, terminating:\n"+e);
				System.out.println("Processor for client "+cid+" is terminating");
			 	return;
    			} catch (ClassNotFoundException e) {
				System.out.println("Couldn't load drivers: "+e);
				return;
    			}
    		}
    		try {
				socket.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
    	}
    }
    
    private class PBFTListener extends Thread
    {
    	protected boolean signal;
    	PBFTConnection connection;
    	
    	// there is one thread for each client, and pos keeps the position in array clients
    	// if there is 0 in that array, at pos, that means client got response/can send
    	public PBFTListener(PBFTConnection con) {
    		signal = false;
    		connection = con;
    		
    		//this.setDaemon(true);
    	}
    	
    	public void run()
    	{
		System.out.println("PBFT Listener running...");
    		while (true) {
    			if (signal == true) {
    				return;
    			}
    			//synchronized(connection)
    			{
    				try
    				{
					System.out.println("Trying to get something from PBFT...");
					System.out.flush();
    					Object[] data = connection.pbft_receive();
					System.out.println("Got a message from PBFT, will process");
    					if (data == null)
    						continue;

    					PBFTProcessor processor = new PBFTProcessor(data);

    					Thread t = new Thread(processor);
    					t.start();
    				} catch (IOException ioe) {
    					System.out.println("IOException on socket listen: " + ioe);
    					ioe.printStackTrace();
    				} catch (Exception e) {
    					// TODO Auto-generated catch block
    					e.printStackTrace();
    				}
    			}
    		}
    	}
    }
    
    // responsible only for write-only requests
    private class PBFTProcessor implements Runnable {
    	private Integer cid;
        private Serializable data;
        private static final long timeout = 2000;
        
        PBFTProcessor(Object[] data) {
	  System.out.println("Starting new PBFTProcessor");
          this.cid = (Integer)data[0];
          this.data = (Serializable)data[1];
        }
        
        private void execute_sql(Connection dbcon, Serializable command) throws Exception
        {
		System.out.println("PBFTProcessor[execute_sql]");
        	Statement stmt = dbcon.createStatement(ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_READ_ONLY);

        	int res = stmt.executeUpdate((String)command);
		System.out.println("Executed "+command+", with result "+res);

        	// Get the bytes of the serialized object
        	/*
                byte[] buf = bos.toByteArray();
                MessageDigest m=MessageDigest.getInstance("MD5");
                m.update(buf,0,buf.length);
        	 */
        	// and now send the packet back to the client
        	synchronized (wsynchro[cid]) {
        		Socket sock = clients.get(cid);
        		try {
        			ObjectOutput out = new ObjectOutputStream(sock.getOutputStream());
				out.writeObject(cid);
        			out.writeObject(res);
				out.flush();
				System.out.println("Data sent to the client "+cid);
				System.out.println("Socket: "+sock+", address "+sock.getInetAddress()+", port "+sock.getPort());
        			//out.close();
        		} catch (IOException e) {
				System.out.println("Caught exception when sending data back to the client\n"+e);
        		}
        	}
        	//          pbft_send(0,crset);
        }

        public void run () {
        	Connection con = null;
        	// get the connection to the db
        	try
        	{
			System.out.println("Trying to get a connection to the db: ");
        		con = pool.getConnection(timeout);
			System.out.println("Got a connection to the db: "+con);
        		if (con != null)
        		{
        			// ...use the connection...
        			execute_sql(con, data);
        		}
        		else
        		{
        			// ...do something else (timeout occurred)...
        		}
        	}
        	catch (SQLException sqle)
        	{
        		// ...deal with exception...
			System.out.println("Got exception: "+sqle);
			System.out.println("Got exception: "+sqle.getCause()+", "+sqle.getSQLState());

        	}
        	catch (Exception e)
        	{
        		System.out.println("Got ordinary exception: "+e+"\nreason: "+e.getCause());	
        	}
        	finally
        	{
        		try {
        			if (con != null)
        				con.close();
        			}
        		catch (SQLException e) { /* ... */ }
        	}
		System.out.println("PBFTProcessor finished");
        }
    }
}

